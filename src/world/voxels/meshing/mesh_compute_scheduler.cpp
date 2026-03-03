#include "mesh_compute_scheduler.h"
#include "extracted_mesh_data.h"
#include "voxel_terrain.h"
// #include "adaptive_surface_nets/adaptive_surface_nets.h"
#include "dual_surface_extractor.h"
// #include "stitched_dual_contouring.h"
#include "voxel_octree_node.h"
#include <memory>

using namespace godot;

MeshComputeScheduler::MeshComputeScheduler(int maxConcurrentTasks)
    : _maxConcurrentTasks(maxConcurrentTasks), _activeTasks(0), _totalTris(0), _prevTris(0),
      threadPool(maxConcurrentTasks)
{
}

void MeshComputeScheduler::enqueue(VoxelOctreeNode &node)
{
    ChunksToAdd.push(&node);
}

void MeshComputeScheduler::process(JarVoxelTerrain &terrain)
{
    _prevTris = _totalTris;
    if (!terrain.is_building())
    {
        process_queue(terrain);
    }
    while (!ChunksToProcess.empty())
    {
        std::pair<VoxelOctreeNode *, ExtractedMeshData *> tuple;
        if (ChunksToProcess.try_pop(tuple))
        {
            auto [node, chunkMeshData] = tuple;
            std::unique_ptr<ExtractedMeshData> chunk_mesh_data_guard(chunkMeshData);
            if (node == nullptr)
                continue;
            node->update_chunk(terrain, chunkMeshData);
        }
    }
}

void MeshComputeScheduler::process_queue(JarVoxelTerrain &terrain)
{
    while (_activeTasks.load(std::memory_order_relaxed) < _maxConcurrentTasks && !ChunksToAdd.empty())
    {
        VoxelOctreeNode *chunk;
        if (ChunksToAdd.try_pop(chunk))
        {
            run_task(terrain, *chunk);
        }
        else
            return;
    }
}

void MeshComputeScheduler::run_task(const JarVoxelTerrain &terrain, VoxelOctreeNode &chunk)
{
    if (!chunk.is_chunk(terrain))
        return;

    _activeTasks.fetch_add(1, std::memory_order_relaxed);

    threadPool.enqueue([this, &terrain, &chunk]()
                       {
        std::unique_ptr<MeshExtractor> meshCompute;
        switch (_meshAlgorithm)
        {
        case MeshAlgorithm::STITCHED_DUAL_CONTOURING:
            // meshCompute = new StitchedDualContouring(terrain);
            break;
        case MeshAlgorithm::STITCHED_SURFACE_NETS:
        default:
            meshCompute = std::make_unique<DualSurfaceExtractor>(terrain);
            break;
        }

        ExtractedMeshData *chunkMeshData = nullptr;
        if (meshCompute)
            chunkMeshData = meshCompute->generate_mesh_data(terrain, chunk);

        ChunksToProcess.push(std::make_pair(&(chunk), chunkMeshData));
        _activeTasks.fetch_sub(1, std::memory_order_relaxed); });
}

void MeshComputeScheduler::clear_queue()
{
    // if we readd this, ensure to unenqueue all nodes!
    //  ChunksToAdd.clear();
    //  ChunksToProcess.clear();
}
