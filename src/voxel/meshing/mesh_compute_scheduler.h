#ifndef MESH_COMPUTE_SCHEDULER_H
#define MESH_COMPUTE_SCHEDULER_H

#include "voxel_octree_node.h"
#include <atomic>
#include <functional>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>
#include "utility/job_system.h"
#include "utility/concurrent_priority_queue.h"

using namespace godot;

class JarVoxelVolume;
class ChunkMeshData;

// Meshes the chunks that builds and edits queue, and shows the results in
// groups: a chunk's mesh depends on its neighbors' (their LODs and the samples
// along the border), so chunks queued together that touch, or that replace
// the same shown chunk, are shown in the same frame (shown one by one, chunks
// next to a neighbor still meshed for its old LOD left cracks, and finer
// chunks over the coarse one they replace overlapped it). Separate groups are
// shown as soon as each is ready: an edit's chunks no longer wait for a whole
// LOD update elsewhere, and big updates are spread over frames.
//
// Edits may change the octree while groups are pending, as long as no mesh
// task runs (is_running()): only a few tasks are in flight at a time (edit
// chunks first), so an edit waits a few milliseconds, not for a whole LOD
// update. A pending chunk an edit touches is meshed again before its group
// is shown. LOD builds still wait until nothing is pending (is_meshing()).
class MeshComputeScheduler
{
  private:
    static constexpr int ApplyBudgetMicroseconds = 4000;

    // From the build worker (any of its threads): chunks queued by the build
    // or edit running now, and once it is done, their relations (see
    // close_batch). Drained by the main thread.
    struct Incoming
    {
        VoxelOctreeNode *node;
        bool urgent;
        std::vector<VoxelOctreeNode *> related; // chunks it touches, shown chunks it replaces
    };
    std::mutex _incomingMutex;
    std::vector<Incoming> _incoming; // queued, not yet closed into a batch
    std::vector<Incoming> _closed;   // closed batches, ready for the main thread

    // Main thread: every chunk queued and not yet shown.
    enum State : uint8_t
    {
        Queued,  // waiting for a task
        Running, // a task meshes it
        Ready,   // result uploaded (or empty), waiting for its group
    };
    struct Entry
    {
        VoxelOctreeNode *node = nullptr;
        ChunkMeshData *result = nullptr;
        State state = Queued;
        bool stale = false; // changed by an edit while running or done: meshed again
        int parent = -1;    // union-find (-1: a root)
        // Roots only: the group's entries, and how many are not Ready.
        std::vector<int> members;
        int notReady = 0;
    };
    std::vector<Entry> _entries;
    std::vector<int> _freeEntries;
    std::unordered_map<const VoxelOctreeNode *, int> _entryOf;
    // Tie-break related chunks to entries: shown chunks (not themselves
    // queued) stand for the queued chunks replacing them.
    std::unordered_map<const VoxelOctreeNode *, int> _replacing;
    std::deque<int> _urgent; // edit chunks
    struct ByLod
    {
        const std::vector<Entry> *entries;
        bool operator()(int a, int b) const
        {
            return (*entries)[a].node->get_lod() > (*entries)[b].node->get_lod(); // finest first
        }
    };
    std::priority_queue<int, std::vector<int>, ByLod> _normal{ByLod{&_entries}};
    int _pendingTotal = 0; // entries alive

    std::atomic<int> _activeTasks{0};
    ConcurrentPriorityQueue<std::pair<VoxelOctreeNode *, ChunkMeshData *>> _results;

    int find(int id);
    void unite(int a, int b);
    int new_entry(VoxelOctreeNode *node);
    void requeue(int id, bool urgent);
    void drain_incoming(JarVoxelVolume &terrain);
    void dispatch(JarVoxelVolume &terrain);
    void collect_results();
    void commit_ready_groups(JarVoxelVolume &terrain);
    void run_task(const JarVoxelVolume &terrain, VoxelOctreeNode &chunk);
    std::vector<int> _readyRoots; // groups that became complete

  public:
    MeshComputeScheduler();
    // Waits for this volume's tasks on the shared pool, then frees unapplied results.
    ~MeshComputeScheduler();
    // Build worker: `node` (a chunk) needs a new mesh; edits' chunks are urgent.
    void enqueue(VoxelOctreeNode &node, bool urgent);
    // Build worker, at the end of a build or edit (the octree settled): finds
    // how the chunks it queued relate, and hands them to the main thread.
    void close_batch(const JarVoxelVolume &terrain);
    // Main thread, every frame (not while building): starts tasks (if
    // `dispatch`), uploads results and shows the groups that are complete.
    // `waitMicroseconds`: how long to wait for urgent tasks (a fresh edit's)
    // to finish, so the edit shows this frame.
    void process(JarVoxelVolume &terrain, bool dispatch, int waitMicroseconds = 0);

    // A mesh task holds octree node pointers: the octree must not change.
    bool is_running() const { return _activeTasks > 0; }
    // Anything queued, meshing, or not shown yet: LOD builds wait for this.
    bool is_meshing();
};

#endif // MESH_COMPUTE_SCHEDULER_H
