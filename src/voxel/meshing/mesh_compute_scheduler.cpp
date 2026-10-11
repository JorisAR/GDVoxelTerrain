#include "mesh_compute_scheduler.h"
#include "chunk_mesh_data.h"
#include "voxel_volume.h"
#include "stitched_surface_nets/stitched_surface_nets.h"
#include "voxel_octree_node.h"
#include "chunk_detail_generator.h"
#include <chrono>

MeshComputeScheduler::MeshComputeScheduler() : _totalTris(0), _prevTris(0)
{
}

MeshComputeScheduler::~MeshComputeScheduler()
{
    // Tasks on the shared pool reference this scheduler and its volume.
    while (_activeTasks > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::pair<VoxelOctreeNode *, ChunkMeshData *> tuple;
    while (ChunksToProcess.try_pop(tuple))
        delete tuple.second;
}

void MeshComputeScheduler::enqueue(VoxelOctreeNode &node)
{
    ChunksToAdd.push(&node);
}

void MeshComputeScheduler::process(JarVoxelVolume &terrain)
{
    _prevTris = _totalTris;
    if (!terrain.is_building())
    {
        process_queue(terrain);
    }
    // Applying a mesh (ArrayMesh upload, chunk acquire, details) runs on the main
    // thread. Spread it over frames: applying thousands at once after a build
    // stalled a frame for 200+ ms.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(ApplyBudgetMicroseconds);
    std::pair<VoxelOctreeNode *, ChunkMeshData *> tuple;
    while (ChunksToProcess.try_pop(tuple))
    {
        auto [node, chunkMeshData] = tuple;
        node->update_chunk(terrain, chunkMeshData);
        if (std::chrono::steady_clock::now() >= deadline)
            break;
    }
}

void MeshComputeScheduler::process_queue(JarVoxelVolume &terrain)
{
    while (!ChunksToAdd.empty())
    {
        VoxelOctreeNode *chunk;
        if (ChunksToAdd.try_pop(chunk))
        {
            if (chunk->is_chunk(terrain))
                run_task(terrain, *chunk);
            else
                chunk->cancel_enqueue();
        }
        else
            return;
    }
}

void MeshComputeScheduler::run_task(const JarVoxelVolume &terrain, VoxelOctreeNode &chunk)
{
    _activeTasks++;
    JobSystem::get_pool().enqueue([this, &terrain, &chunk]() {
        auto meshCompute = StitchedSurfaceNets(terrain, chunk);
        ChunkMeshData *chunkMeshData = meshCompute.generate_mesh_data(terrain);
        if (chunkMeshData != nullptr && chunkMeshData->lod <= ChunkMeshData::MaxCollisionLod)
            chunkMeshData->build_collision_mesh();
        if (chunkMeshData != nullptr && chunkMeshData->lod <= 0)
        {
            ChunkDetailGenerator generator(terrain.get_up_field());
            chunkMeshData->detail_buffers = generator.generate_detail_buffers(terrain.get_terrain_details(), *chunkMeshData);
        }
        ChunksToProcess.push(std::make_pair(&(chunk), chunkMeshData));
        _activeTasks--; // after the push, so is_meshing() never sees a gap
    });
}

void MeshComputeScheduler::clear_queue()
{
    //if we readd this, ensure to unenqueue all nodes!
    // ChunksToAdd.clear();
    // ChunksToProcess.clear();
}
