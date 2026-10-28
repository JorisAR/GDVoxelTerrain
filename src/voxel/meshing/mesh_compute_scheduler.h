#ifndef MESH_COMPUTE_SCHEDULER_H
#define MESH_COMPUTE_SCHEDULER_H

#include "voxel_octree_node.h"
#include <atomic>
#include <functional>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <thread>
#include "utility/job_system.h"
#include "utility/concurrent_priority_queue.h"

using namespace godot;

class JarVoxelVolume;


struct ChunkComparator {
    bool operator()(const VoxelOctreeNode *a, const VoxelOctreeNode *b) const {
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
    static constexpr int ApplyBudgetMicroseconds = 4000;

    ConcurrentPriorityQueue<VoxelOctreeNode*, ChunkComparator> ChunksToAdd;
    ConcurrentPriorityQueue<std::pair<VoxelOctreeNode*, ChunkMeshData*>> ChunksToProcess;

    std::atomic<int> _activeTasks{0};
    // Results uploaded but not shown yet: a build's meshes are all shown in the
    // same frame, once the last one is ready (see process()).
    std::vector<std::pair<VoxelOctreeNode *, ChunkMeshData *>> _uploaded;

    // Debug variables
    int _totalTris;
    int _prevTris;

    void process_queue(JarVoxelVolume &terrain);
    void run_task(const JarVoxelVolume &terrain, VoxelOctreeNode &chunk);

  public:
    MeshComputeScheduler();
    // Waits for this volume's tasks on the shared pool, then frees unapplied results.
    ~MeshComputeScheduler();
    void enqueue(VoxelOctreeNode &node);
    void process(JarVoxelVolume &terrain);
    void clear_queue();

    // True while any mesh task holds octree node pointers: queued, running on
    // the pool, or finished but not yet applied on the main thread. The octree
    // must not be mutated (build / edit) while this is true.

    bool is_meshing()
    {
        return _activeTasks > 0 || !ChunksToAdd.empty() || !ChunksToProcess.empty() || !_uploaded.empty();
    }
};

#endif // MESH_COMPUTE_SCHEDULER_H
