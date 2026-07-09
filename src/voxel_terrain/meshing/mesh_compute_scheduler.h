#ifndef MESH_COMPUTE_SCHEDULER_H
#define MESH_COMPUTE_SCHEDULER_H

#include "voxel_octree_node.h"
#include <atomic>
// <concurrent_queue.h> ships with the MSVC ConcRT/PPL runtime only. Key the
// guard on the *compiler* (_MSC_VER), not the OS: mingw defines _WIN32 but is
// GCC and has no such header, so it must take the portable path too.
#ifdef _MSC_VER
#include <concurrent_queue.h>
#include <concurrent_priority_queue.h>
#else
#include "utility/portable_concurrent_queue.h"
#endif
#include <functional>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <thread>
#include "utility/thread_pool.h"

using namespace godot;

class JarVoxelTerrain;


struct ChunkComparator {
    bool operator()(const VoxelOctreeNode *a, const VoxelOctreeNode *b) const {
        return a->get_lod() > b->get_lod();
    }
};

class MeshComputeScheduler
{
  private:
    concurrency::concurrent_priority_queue<VoxelOctreeNode*, ChunkComparator> ChunksToAdd;
    concurrency::concurrent_queue<std::pair<VoxelOctreeNode*, ChunkMeshData*>> ChunksToProcess;

    std::atomic<int> _activeTasks;
    int _maxConcurrentTasks;

    ThreadPool threadPool;

    // Debug variables
    int _totalTris;
    int _prevTris;

    void process_queue(JarVoxelTerrain &terrain);
    void run_task(const JarVoxelTerrain &terrain, VoxelOctreeNode &chunk);

  public:
    MeshComputeScheduler(int maxConcurrentTasks);
    void enqueue(VoxelOctreeNode &node);
    void process(JarVoxelTerrain &terrain);
    void clear_queue();

    bool is_meshing()
    {
        return !ChunksToAdd.empty();
    }
};

#endif // MESH_COMPUTE_SCHEDULER_H
