#include "mesh_compute_scheduler.h"
#include "chunk_mesh_data.h"
#include "voxel_volume.h"
#include "stitched_surface_nets/stitched_surface_nets.h"
#include "voxel_octree_node.h"
#include <chrono>
#include <unordered_map>
#include "voxel_stats.h"

MeshComputeScheduler::MeshComputeScheduler()
{
}

MeshComputeScheduler::~MeshComputeScheduler()
{
    // Tasks on the shared pool reference this scheduler and its volume.
    while (_activeTasks > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::pair<VoxelOctreeNode *, ChunkMeshData *> tuple;
    while (_results.try_pop(tuple))
        delete tuple.second;
    for (Entry &entry : _entries)
        delete entry.result;
    for (ChunkMeshData *result : _outdated)
        delete result;
}

void MeshComputeScheduler::enqueue(VoxelOctreeNode &node, bool urgent)
{
    std::lock_guard<std::mutex> lock(_incomingMutex);
    _incoming.push_back({&node, urgent, {}});
}

void MeshComputeScheduler::close_batch(const JarVoxelVolume &terrain)
{
    std::vector<Incoming> batch;
    {
        std::lock_guard<std::mutex> lock(_incomingMutex);
        batch.swap(_incoming);
    }
    if (batch.empty())
        return;
    // What each chunk's mesh is tied to: the chunks it touches (their
    // borders depend on each other) and the shown chunks it replaces (a coarse
    // one is replaced by all the finer ones inside it together).
    JobSystem::get_pool().parallel_for(batch.size(), [&](size_t i) {
        VoxelOctreeNode &node = *batch[i].node;
        const float voxel = terrain.get_voxel_size();
        terrain.get_chunks_touching(node.get_bounds(voxel).expanded(0.01f * voxel), batch[i].related);
        node.get_shown_chunks_replaced(batch[i].related);
    });
    for (const Incoming &incoming : batch)
    {
        const auto it = _entryOf.find(incoming.node);
        if (it == _entryOf.end())
        {
            const int id = new_entry(incoming.node);
            if (incoming.urgent)
                _urgent.push_back(id);
            else
                _normal.push(id);
        }
        else
            requeue(it->second, incoming.urgent); // an edit changed a chunk not shown yet
    }
    // Shown chunks being replaced tie together the chunks replacing them.
    std::unordered_map<const VoxelOctreeNode *, int> replacing;
    for (const Incoming &incoming : batch)
    {
        const int id = _entryOf[incoming.node];
        for (const VoxelOctreeNode *related : incoming.related)
        {
            if (related == incoming.node)
                continue;
            const auto pending = _entryOf.find(related);
            if (pending != _entryOf.end())
                unite(id, pending->second);
            else if (related->get_chunk() != nullptr)
            {
                const auto other = replacing.emplace(related, id);
                if (!other.second)
                    unite(id, other.first->second);
            }
        }
    }
    // An edit's chunks show with their group: the whole group goes first.
    for (const Incoming &incoming : batch)
        if (incoming.urgent)
            for (const int member : _entries[find(_entryOf[incoming.node])].members)
                if (_entries[member].state == Queued)
                    _urgent.push_back(member);
}

int MeshComputeScheduler::find(int id)
{
    while (_entries[id].parent >= 0)
    {
        const int up = _entries[id].parent;
        if (_entries[up].parent >= 0)
            _entries[id].parent = _entries[up].parent; // path halving
        id = up;
    }
    return id;
}

void MeshComputeScheduler::unite(int a, int b)
{
    a = find(a);
    b = find(b);
    if (a == b)
        return;
    if (_entries[a].members.size() < _entries[b].members.size())
        std::swap(a, b);
    Entry &root = _entries[a];
    Entry &other = _entries[b];
    root.members.insert(root.members.end(), other.members.begin(), other.members.end());
    root.notReady += other.notReady;
    other.members.clear();
    other.members.shrink_to_fit();
    other.notReady = 0;
    other.parent = a;
    if (root.notReady == 0)
        _readyRoots.push_back(a);
}

int MeshComputeScheduler::new_entry(VoxelOctreeNode *node)
{
    int id;
    if (!_freeEntries.empty())
    {
        id = _freeEntries.back();
        _freeEntries.pop_back();
        _entries[id] = Entry();
    }
    else
    {
        id = int(_entries.size());
        _entries.emplace_back();
    }
    Entry &entry = _entries[id];
    entry.node = node;
    entry.members.push_back(id);
    entry.notReady = 1;
    _entryOf[node] = id;
    _pendingTotal++;
    return id;
}

void MeshComputeScheduler::requeue(int id, bool urgent)
{
    Entry &entry = _entries[id];
    switch (entry.state)
    {
    case Queued:
        break; // not meshed yet: it will see the change
    case Running:
        entry.stale = true; // meshed again when this result comes in
        return;
    case Ready:
        _outdated.push_back(entry.result);
        entry.result = nullptr;
        entry.state = Queued;
        _entries[find(id)].notReady++;
        break;
    }
    // Duplicates are harmless: dispatch() skips entries no longer Queued.
    if (urgent)
        _urgent.push_back(id);
    else
        _normal.push(id);
}

void MeshComputeScheduler::dispatch(JarVoxelVolume &terrain)
{
    // A few tasks at a time (the pool's threads, twice over to keep them
    // busy): an edit waits for these to finish, not for all queued.
    const int limit = 2 * int(JobSystem::get_pool().thread_count());
    while (_activeTasks < limit && (!_urgent.empty() || !_normal.empty()))
    {
        int id;
        if (!_urgent.empty())
        {
            id = _urgent.front();
            _urgent.pop_front();
        }
        else
        {
            id = _normal.top();
            _normal.pop();
        }
        Entry &entry = _entries[id];
        if (entry.node == nullptr || entry.state != Queued)
            continue; // a duplicate, or freed since
        if (!entry.node->is_chunk(terrain))
        {
            // No longer a chunk: shown with its group as nothing (its
            // update_chunk drops it and lets go of what it replaced).
            entry.state = Ready;
            const int root = find(id);
            if (--_entries[root].notReady == 0)
                _readyRoots.push_back(root);
            continue;
        }
        entry.state = Running;
        run_task(terrain, *entry.node);
    }
}

void MeshComputeScheduler::collect_results(JarVoxelVolume &terrain)
{
    // Uploading a mesh to the GPU runs on the main thread. Spread it over
    // frames: uploading thousands at once after a build stalled a frame for
    // 200+ ms.
    const VoxelStats::Scope upload(VoxelStats::MainUpload);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(ApplyBudgetMicroseconds);
    std::pair<VoxelOctreeNode *, ChunkMeshData *> tuple;
    while (std::chrono::steady_clock::now() < deadline && _results.try_pop(tuple))
    {
        const int id = _entryOf.at(tuple.first);
        Entry &entry = _entries[id];
        if (entry.stale)
        {
            // An edit changed it while this was being made.
            delete tuple.second;
            entry.stale = false;
            entry.state = Queued;
            _urgent.push_back(id);
            continue;
        }
        if (tuple.second != nullptr)
        {
            tuple.second->upload();
            tuple.first->prepare_chunk(terrain, *tuple.second);
        }
        entry.result = tuple.second;
        entry.state = Ready;
        const int root = find(id);
        if (--_entries[root].notReady == 0)
            _readyRoots.push_back(root);
    }
}

void MeshComputeScheduler::commit_ready_groups(JarVoxelVolume &terrain)
{
    std::vector<int> roots;
    roots.swap(_readyRoots);
    bool committed = false;
    for (const int root : roots)
    {
        Entry &group = _entries[root];
        // Merged into another group, requeued by an edit, or shown already.
        if (group.node == nullptr || group.parent >= 0 || group.notReady != 0)
            continue;
        const VoxelStats::Scope commit(VoxelStats::MainCommit);
        VoxelStats::add_count(VoxelStats::ChunksCommitted, int64_t(group.members.size()));
        const std::vector<int> members = std::move(group.members);
        for (const int id : members)
        {
            Entry &entry = _entries[id];
            VoxelOctreeNode *node = entry.node;
            ChunkMeshData *result = entry.result;
            _entryOf.erase(node);
            entry = Entry();
            _freeEntries.push_back(id);
            _pendingTotal--;
            node->update_chunk(terrain, result);
        }
        committed = true;
    }
    if (committed)
    {
        terrain.process_pool(); // the replaced chunks go in the same frame
        terrain.notify_meshes_committed();
    }
}

void MeshComputeScheduler::process(JarVoxelVolume &terrain, bool dispatchTasks, int waitMicroseconds)
{
    for (ChunkMeshData *result : _outdated)
    {
        if (result != nullptr)
            terrain.pool_release(result->prepared);
        delete result;
    }
    _outdated.clear();
    if (dispatchTasks)
    {
        if (waitMicroseconds > 0)
        {
            // A fresh edit: its chunks alone first, then wait for them.
            std::deque<int> urgent;
            urgent.swap(_urgent);
            std::priority_queue<int, std::vector<int>, ByLod> normal{ByLod{&_entries}};
            std::swap(normal, _normal);
            _urgent.swap(urgent);
            dispatch(terrain);
            std::swap(normal, _normal);
            const VoxelStats::Scope wait(VoxelStats::MainEditWait);
            const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(waitMicroseconds);
            while (_activeTasks > 0 && std::chrono::steady_clock::now() < until)
                std::this_thread::yield();
        }
        dispatch(terrain);
    }
    VoxelStats::set_gauge(VoxelStats::MeshQueue, _pendingTotal);
    collect_results(terrain);
    commit_ready_groups(terrain);
}

bool MeshComputeScheduler::is_meshing()
{
    if (_activeTasks > 0 || _pendingTotal > 0)
        return true;
    std::lock_guard<std::mutex> lock(_incomingMutex);
    return !_incoming.empty();
}

void MeshComputeScheduler::run_task(const JarVoxelVolume &terrain, VoxelOctreeNode &chunk)
{
    _activeTasks++;
    JobSystem::get_pool().enqueue([this, &terrain, &chunk]() {
        ChunkMeshData *chunkMeshData;
        {
            const VoxelStats::Scope stage(VoxelStats::WorkerMesh);
            auto meshCompute = StitchedSurfaceNets(terrain, chunk);
            chunkMeshData = meshCompute.generate_mesh_data(terrain);
            if (chunkMeshData != nullptr && chunkMeshData->lod <= ChunkMeshData::MaxCollisionLod &&
                terrain.colliders_enabled())
            {
                const VoxelStats::Scope collider(VoxelStats::WorkerCollider);
                chunkMeshData->build_collision_mesh();
            }
        }
        if (chunkMeshData == nullptr)
            chunk.set_empty_boundaries(chunk.compute_boundaries(terrain));
        VoxelStats::add_count(VoxelStats::ChunksMeshed);
        _results.push(std::make_pair(&(chunk), chunkMeshData));
        _activeTasks--; // after the push, so is_meshing() never sees a gap
    });
}
