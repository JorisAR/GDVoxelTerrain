#ifndef MESH_COMPUTE_SCHEDULER_H
#define MESH_COMPUTE_SCHEDULER_H

#include "voxel_octree_node.h"
#include <atomic>
#include <functional>
#include <memory>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <thread>
#include "utility/thread_pool.h"
#include "utility/concurrent_priority_queue.h"

namespace godot
{

    class JarVoxelTerrain;

    enum class MeshAlgorithm
    {
        STITCHED_SURFACE_NETS,
        STITCHED_DUAL_CONTOURING,
        ADAPTIVE_SURFACE_NETS
    };

    struct ChunkComparator
    {
        bool operator()(const VoxelOctreeNode *a, const VoxelOctreeNode *b) const
        {
            return a->get_lod() > b->get_lod();
        }
    };
    // struct ChunkComparator2 {
    //     bool operator()(const std::pair<VoxelOctreeNode*, ChunkMeshData*> a, const std::pair<VoxelOctreeNode*, ChunkMeshData*> b) const {
    //         return a.first->get_lod() > b.first->get_lod();
    //     }
    // };

    class MeshComputeScheduler
    {
    private:
        ConcurrentPriorityQueue<VoxelOctreeNode *, ChunkComparator> ChunksToAdd;
        ConcurrentPriorityQueue<std::pair<VoxelOctreeNode *, ExtractedMeshData *>> ChunksToProcess;

        std::atomic<int> _activeTasks;
        int _maxConcurrentTasks;

        ThreadPool threadPool;

        // Debug variables
        int _totalTris;
        int _prevTris;

        MeshAlgorithm _meshAlgorithm = MeshAlgorithm::STITCHED_SURFACE_NETS;

        void process_queue(JarVoxelTerrain &terrain);
        void run_task(const JarVoxelTerrain &terrain, VoxelOctreeNode &chunk);

    public:
        MeshComputeScheduler(int maxConcurrentTasks);
        void enqueue(VoxelOctreeNode &node);
        void process(JarVoxelTerrain &terrain);
        void clear_queue();

        bool is_meshing() const
        {
            return !ChunksToAdd.empty() || !ChunksToProcess.empty() || _activeTasks.load(std::memory_order_relaxed) > 0;
        }
    };
}

#endif // MESH_COMPUTE_SCHEDULER_H
