#include "voxel_terrain.h"
#include "modify_settings.h"
#include "plane_sdf.h"
#include "sphere_sdf.h"
#include "voxel_chunk.h"

void JarVoxelTerrain::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_player_node"), &JarVoxelTerrain::get_player_node);
    ClassDB::bind_method(D_METHOD("set_player_node", "player_node"), &JarVoxelTerrain::set_player_node);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player_node", PROPERTY_HINT_NODE_TYPE, "Node3D"), "set_player_node",
                 "get_player_node");

    ClassDB::bind_method(D_METHOD("get_world_node"), &JarVoxelTerrain::get_world_node);
    ClassDB::bind_method(D_METHOD("set_world_node", "world_node"), &JarVoxelTerrain::set_world_node);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "world_node", PROPERTY_HINT_NODE_TYPE, "JarWorld"), "set_world_node",
                 "get_world_node");

    ClassDB::bind_method(D_METHOD("get_octree_scale"), &JarVoxelTerrain::get_octree_scale);
    ClassDB::bind_method(D_METHOD("set_octree_scale", "value"), &JarVoxelTerrain::set_octree_scale);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "octree_scale"), "set_octree_scale", "get_octree_scale");

    ClassDB::bind_method(D_METHOD("get_size"), &JarVoxelTerrain::get_size);
    ClassDB::bind_method(D_METHOD("set_size", "value"), &JarVoxelTerrain::set_size);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "size"), "set_size", "get_size");

    ClassDB::bind_method(D_METHOD("get_min_chunk_size"), &JarVoxelTerrain::get_min_chunk_size);
    ClassDB::bind_method(D_METHOD("set_min_chunk_size", "value"), &JarVoxelTerrain::set_min_chunk_size);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "min_chunk_size"), "set_min_chunk_size", "get_min_chunk_size");

    ClassDB::bind_method(D_METHOD("get_chunk_scene"), &JarVoxelTerrain::get_chunk_scene);
    ClassDB::bind_method(D_METHOD("set_chunk_scene", "value"), &JarVoxelTerrain::set_chunk_scene);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "chunk_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
                 "set_chunk_scene", "get_chunk_scene");

    ClassDB::bind_method(D_METHOD("get_sdf"), &JarVoxelTerrain::get_sdf);
    ClassDB::bind_method(D_METHOD("set_sdf", "sdf"), &JarVoxelTerrain::set_sdf);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sdf", PROPERTY_HINT_RESOURCE_TYPE, "JarSignedDistanceField"), "set_sdf",
                 "get_sdf");

    ClassDB::bind_method(D_METHOD("get_cubic_voxels"), &JarVoxelTerrain::get_cubic_voxels);
    ClassDB::bind_method(D_METHOD("set_cubic_voxels", "cubic_voxels"), &JarVoxelTerrain::set_cubic_voxels);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cubic_voxels"), "set_cubic_voxels", "get_cubic_voxels");

    // -------------------------------------------------- PERFORMANCE --------------------------------------------------
    ADD_GROUP("Performance", "performance_");
    ClassDB::bind_method(D_METHOD("get_max_concurrent_tasks"), &JarVoxelTerrain::get_max_concurrent_tasks);
    ClassDB::bind_method(D_METHOD("set_max_concurrent_tasks", "value"), &JarVoxelTerrain::set_max_concurrent_tasks);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "performance_max_concurrent_tasks"), "set_max_concurrent_tasks",
                 "get_max_concurrent_tasks");

    ClassDB::bind_method(D_METHOD("get_updated_colliders_per_second"),
                         &JarVoxelTerrain::get_updated_colliders_per_second);
    ClassDB::bind_method(D_METHOD("set_updated_colliders_per_second", "value"),
                         &JarVoxelTerrain::set_updated_colliders_per_second);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "performance_updated_colliders_per_second"),
                 "set_updated_colliders_per_second", "get_updated_colliders_per_second");

    ClassDB::bind_method(D_METHOD("get_collider_distance"), &JarVoxelTerrain::get_collider_distance);
    ClassDB::bind_method(D_METHOD("set_collider_distance", "value"), &JarVoxelTerrain::set_collider_distance);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "performance_collider_distance", PROPERTY_HINT_RANGE, "0,4096,1,or_greater"),
                 "set_collider_distance", "get_collider_distance");

    // -------------------------------------------------- LOD --------------------------------------------------
    ADD_GROUP("Level Of Detail", "lod_");
    ClassDB::bind_method(D_METHOD("get_lod_level_count"), &JarVoxelTerrain::get_lod_level_count);
    ClassDB::bind_method(D_METHOD("set_lod_level_count", "value"), &JarVoxelTerrain::set_lod_level_count);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "lod_level_count"), "set_lod_level_count", "get_lod_level_count");

    ClassDB::bind_method(D_METHOD("get_lod_shell_size"), &JarVoxelTerrain::get_lod_shell_size);
    ClassDB::bind_method(D_METHOD("set_lod_shell_size", "value"), &JarVoxelTerrain::set_lod_shell_size);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "lod_shell_size"), "set_lod_shell_size", "get_lod_shell_size");

    ClassDB::bind_method(D_METHOD("get_lod_automatic_update"), &JarVoxelTerrain::get_lod_automatic_update);
    ClassDB::bind_method(D_METHOD("set_lod_automatic_update", "value"), &JarVoxelTerrain::set_lod_automatic_update);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "lod_automatic_update"), "set_lod_automatic_update",
                 "get_lod_automatic_update");

    ClassDB::bind_method(D_METHOD("get_lod_automatic_update_distance"),
                         &JarVoxelTerrain::get_lod_automatic_update_distance);
    ClassDB::bind_method(D_METHOD("set_lod_automatic_update_distance", "value"),
                         &JarVoxelTerrain::set_lod_automatic_update_distance);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_automatic_update_distance"), "set_lod_automatic_update_distance",
                 "get_lod_automatic_update_distance");

    //-------------------------------------------------- POPULATION --------------------------------------------------
    ADD_GROUP("Population", "population_");
    ClassDB::bind_method(D_METHOD("get_terrain_details"), &JarVoxelTerrain::get_terrain_details);
    ClassDB::bind_method(D_METHOD("set_terrain_details", "value"), &JarVoxelTerrain::set_terrain_details);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "population_terrain_details", PROPERTY_HINT_TYPE_STRING,
                              String::num(Variant::OBJECT) + "/" + String::num(PROPERTY_HINT_RESOURCE_TYPE) +
                                  ":JarTerrainDetail"),
                 "set_terrain_details", "get_terrain_details");

    BIND_ENUM_CONSTANT(SDF::SDF_OPERATION_UNION);
    BIND_ENUM_CONSTANT(SDF::SDF_OPERATION_SUBTRACTION);
    BIND_ENUM_CONSTANT(SDF::SDF_OPERATION_INTERSECTION);
    BIND_ENUM_CONSTANT(SDF::SDF_OPERATION_SMOOTH_UNION);
    BIND_ENUM_CONSTANT(SDF::SDF_OPERATION_SMOOTH_SUBTRACTION);
    BIND_ENUM_CONSTANT(SDF::SDF_OPERATION_SMOOTH_INTERSECTION);
    ClassDB::bind_method(D_METHOD("modify", "sdf", "operation", "position", "radius"), &JarVoxelTerrain::modify);
    ClassDB::bind_method(D_METHOD("sphere_edit", "position", "radius", "union"), &JarVoxelTerrain::sphere_edit);
    ClassDB::bind_method(D_METHOD("spawn_debug_spheres_in_bounds", "position", "range"),
                         &JarVoxelTerrain::spawn_debug_spheres_in_bounds);
    ClassDB::bind_method(D_METHOD("force_update_lod"), &JarVoxelTerrain::force_update_lod);
}

JarVoxelTerrain::JarVoxelTerrain() : _octreeScale(1.0f), _size(14), _playerNode(nullptr)
{
    _chunkSize = (1 << _minChunkSize);
}

JarVoxelTerrain::~JarVoxelTerrain()
{
    stop_build_worker();
}

// Persistent worker: sleeps until a build is requested, runs one octree build,
// then sleeps again. One thread for the terrain's whole lifetime instead of a
// fresh detached thread per build.
void JarVoxelTerrain::build_worker()
{
    for (;;)
    {
        {
            std::unique_lock<std::mutex> lock(_buildMutex);
            _buildCv.wait(lock, [this] { return _buildRequested || _buildShutdown; });
            if (_buildShutdown)
                return;
            _buildRequested = false;
        }
        if (_voxelRoot)
            _voxelRoot->build(*this);
        _isBuilding = false;
    }
}

// Signal the worker to stop and JOIN it, so no build is in flight once the
// terrain (and its octree) start being destroyed. Fixes the shutdown SIGSEGV.
void JarVoxelTerrain::stop_build_worker()
{
    {
        std::lock_guard<std::mutex> lock(_buildMutex);
        _buildShutdown = true;
    }
    _buildCv.notify_all();
    if (_buildThread.joinable())
        _buildThread.join();
}

// --- chunk object pool ------------------------------------------------------
// Main thread: reuse a parked chunk if one is free, else instantiate a new one.
JarVoxelChunk *JarVoxelTerrain::pool_acquire()
{
    if (!_freeChunks.empty())
    {
        JarVoxelChunk *c = _freeChunks.back();
        _freeChunks.pop_back();
        c->reactivate();
        return c;
    }
    JarVoxelChunk *c = static_cast<JarVoxelChunk *>(_chunkScene->instantiate());
    add_child(c);
    return c;
}

// Thread-safe (called from the build worker): just queue the chunk. The actual
// scene-tree work (hide/disable) happens on the main thread in process_pool().
void JarVoxelTerrain::pool_release(JarVoxelChunk *chunk)
{
    if (chunk == nullptr)
        return;
    std::lock_guard<std::mutex> lock(_poolMutex);
    _releasedChunks.push_back(chunk);
}

// Main thread: park released chunks (hide + collision off) and pool them.
void JarVoxelTerrain::process_pool()
{
    std::vector<JarVoxelChunk *> released;
    {
        std::lock_guard<std::mutex> lock(_poolMutex);
        released.swap(_releasedChunks);
    }
    for (JarVoxelChunk *c : released)
    {
        c->deactivate();
        _freeChunks.push_back(c);
    }
}

void JarVoxelTerrain::modify(const Ref<JarSignedDistanceField> sdf, const SDF::Operation operation,
                             const Vector3 &position, const float radius)
{
    glm::vec3 pos = glm::vec3(position.x, position.y, position.z);
    auto s = new JarSphereSdf();
    s->set_radius(radius);

    auto edge = glm::vec3(radius);
    _modifySettingsQueue.push({s, Bounds(pos - edge, pos + edge), pos, operation});
}

void JarVoxelTerrain::sphere_edit(const Vector3 &position, const float radius, bool operation_union)
{
    auto global_position = position - get_global_position();
    glm::vec3 pos = glm::vec3(global_position.x, global_position.y, global_position.z);
    auto operation = operation_union ? SDF::Operation::SDF_OPERATION_UNION : SDF::Operation::SDF_OPERATION_SUBTRACTION;
    Ref<JarSphereSdf> sdf;
    sdf.instantiate();
    sdf->set_radius(radius);
    auto edge = glm::vec3(radius + _octreeScale * 2.0f);

    if (_isBuilding)
        return;
    ModifySettings settings = {sdf, Bounds(pos - edge, pos + edge), pos, operation};
    _voxelRoot->modify_sdf_in_bounds(*this, settings);
    //_populationRoot->remove_population(settings);
    //_modifySettingsQueue.push({sdf, Bounds(pos - edge, pos + edge), pos, operation});
}

void JarVoxelTerrain::enqueue_chunk_collider(VoxelOctreeNode *node)
{
    if (node == nullptr)
        return;
    _updateChunkCollidersQueue.push(node);
}

void JarVoxelTerrain::enqueue_chunk_update(VoxelOctreeNode &node)
{
    _meshComputeScheduler->enqueue(node);
}

Node3D *JarVoxelTerrain::get_player_node() const
{
    return _playerNode;
}

void JarVoxelTerrain::set_player_node(Node3D *playerNode)
{
    _playerNode = playerNode;
}

JarWorld *JarVoxelTerrain::get_world_node() const
{
    return _worldNode;
}

void JarVoxelTerrain::set_world_node(JarWorld *worldNode)
{
    _worldNode = worldNode;
}

bool JarVoxelTerrain::is_building() const
{
    return _isBuilding;
}

Ref<JarSignedDistanceField> JarVoxelTerrain::get_sdf() const
{
    return _sdf;
}
void JarVoxelTerrain::set_sdf(const Ref<JarSignedDistanceField> &sdf)
{
    _sdf = sdf;
}

// std:: MeshComputeScheduler JarVoxelTerrain::get_mesh_scheduler() const
// {
//     return _meshComputeScheduler;
// }

float JarVoxelTerrain::get_octree_scale() const
{
    return _octreeScale;
}

void JarVoxelTerrain::set_octree_scale(float value)
{
    _octreeScale = value;
}

int JarVoxelTerrain::get_size() const
{
    return _size;
}

void JarVoxelTerrain::set_size(int value)
{
    _size = value;
}

int JarVoxelTerrain::get_min_chunk_size() const
{
    return _minChunkSize;
}

void JarVoxelTerrain::set_min_chunk_size(int value)
{
    _minChunkSize = value;
}

int JarVoxelTerrain::get_chunk_size() const
{
    return _chunkSize;
}

Ref<PackedScene> JarVoxelTerrain::get_chunk_scene() const
{
    return _chunkScene;
}

void JarVoxelTerrain::set_chunk_scene(const Ref<PackedScene> &value)
{
    _chunkScene = value;
}

bool JarVoxelTerrain::get_cubic_voxels() const
{
    return _cubicVoxels;
}

void JarVoxelTerrain::set_cubic_voxels(bool value)
{
    _cubicVoxels = value;
}

int JarVoxelTerrain::get_max_concurrent_tasks() const
{
    return _maxConcurrentTasks;
}
void JarVoxelTerrain::set_max_concurrent_tasks(int value)
{
    _maxConcurrentTasks = value;
}

int JarVoxelTerrain::get_updated_colliders_per_second() const
{
    return _updatedCollidersPerSecond;
}
void JarVoxelTerrain::set_updated_colliders_per_second(int value)
{
    _updatedCollidersPerSecond = value;
}
float JarVoxelTerrain::get_collider_distance() const
{
    return _colliderDistance;
}
void JarVoxelTerrain::set_collider_distance(float value)
{
    _colliderDistance = value;
}
bool JarVoxelTerrain::is_in_collider_range(const glm::vec3 &pos) const
{
    if (_colliderDistance <= 0.0f)
        return true; // disabled -> original behaviour (collider on every near-LOD chunk)
    return glm::distance(pos, _voxelLod.get_camera_position()) <= _colliderDistance;
}

int JarVoxelTerrain::get_lod_level_count() const
{
    return lod_level_count;
}

void JarVoxelTerrain::set_lod_level_count(int value)
{
    lod_level_count = value;
}

int JarVoxelTerrain::get_lod_shell_size() const
{
    return lod_shell_size;
}

void JarVoxelTerrain::set_lod_shell_size(int value)
{
    lod_shell_size = value;
}

bool JarVoxelTerrain::get_lod_automatic_update() const
{
    return lod_automatic_update;
}

void JarVoxelTerrain::set_lod_automatic_update(bool value)
{
    lod_automatic_update = value;
}

float JarVoxelTerrain::get_lod_automatic_update_distance() const
{
    return lod_automatic_update_distance;
}

void JarVoxelTerrain::set_lod_automatic_update_distance(float value)
{
    lod_automatic_update_distance = value;
}

void JarVoxelTerrain::_notification(int p_what)
{
    if (godot::Engine::get_singleton()->is_editor_hint())
    {
        return;
    }
    switch (p_what)
    {
    case NOTIFICATION_ENTER_TREE: {
        initialize();
        set_process_internal(true);
        break;
    }
    case NOTIFICATION_READY: {
        break;
    }
    case NOTIFICATION_EXIT_TREE: {
        set_process_internal(false);
        break;
    }
    case NOTIFICATION_INTERNAL_PROCESS: {
        process();
        break;
    }
    }
}

void JarVoxelTerrain::initialize()
{
    if (_chunkScene.is_null())
    {
        UtilityFunctions::printerr("No ChunkScene properties, please provide it.");
        return;
    }
    if (_sdf.is_null())
    {
        UtilityFunctions::printerr("No sdf, please provide it.");
        return;
    }
    _chunkSize = (1 << _minChunkSize);
    _voxelLod =
        JarVoxelLoD(lod_automatic_update, lod_automatic_update_distance, lod_level_count, lod_shell_size, _octreeScale);
    _meshComputeScheduler = std::make_unique<MeshComputeScheduler>(_maxConcurrentTasks);
    _voxelRoot = std::make_unique<VoxelOctreeNode>(_size);
    //_populationRoot = memnew(PopulationOctreeNode(_size));
    if (!_buildThread.joinable())
        _buildThread = std::thread([this] { build_worker(); });
    build();
}

void JarVoxelTerrain::process()
{
    // While a build thread owns the octree (_isBuilding), NO main-thread tree
    // access may run — meshing dispatch, modify queue, and chunk queue all touch
    // the same nodes the worker is mutating. Letting any of them run concurrently
    // was the heap-corruption / nondeterministic-SIGSEGV race. So bail the whole
    // frame until the build finishes.
    if (_isBuilding)
        return;
    process_pool(); // reclaim chunks the last build released, ready for reuse
    float delta = get_process_delta_time();
    if (!_meshComputeScheduler->is_meshing() && _voxelLod.process(*this, false))
    {
        build();  // spawns the build worker (sets _isBuilding); resume next frame
        return;
    }
    _meshComputeScheduler->process(*this);

    if (!_modifySettingsQueue.empty())
    {
        process_modify_queue();
    }

    process_chunk_queue(delta); // static_cast<float>(delta)
}

void printUniqueLoDValues(const std::vector<int> &lodValues)
{
    godot::String lodString = "Unique LoD values: ";
    for (const auto &lod : lodValues)
    {
        lodString += godot::String::num_int64(lod) + ", ";
    }
    // Remove the last comma and space
    if (lodValues.size() > 0)
    {
        lodString = lodString.substr(0, lodString.length() - 2);
    }
    UtilityFunctions::print(lodString);
}

void JarVoxelTerrain::build()
{
    // The octree build walks/mutates the shared tree on a worker thread. It must
    // NOT overlap (a) another build, or (b) the meshing pool that reads the same
    // nodes -> otherwise: heap corruption / nondeterministic SIGSEGV.
    //  - (a) is prevented by claiming _isBuilding atomically BEFORE spawning
    //        (was set inside the thread => TOCTOU window => 2nd racing build).
    //  - (b) is prevented by gating _meshComputeScheduler->process() on
    //        !_isBuilding in process(), so meshing only runs once a build is done.
    if (_meshComputeScheduler->is_meshing())
        return;
    bool expected = false;
    if (!_isBuilding.compare_exchange_strong(expected, true))
        return; // a build is already in flight

    // Hand the work to the persistent build worker (see build_worker()). It is
    // joined on destruction, so unlike the old detach()ed thread it can't be left
    // running while the terrain is torn down.
    {
        std::lock_guard<std::mutex> lock(_buildMutex);
        _buildRequested = true;
    }
    _buildCv.notify_one();

    // std::thread([this]() { _worldBiomes->update_texture(_levelOfDetail->get_camera_position()); }).detach();
    // UtilityFunctions::print("Done Building.");
    // UtilityFunctions::print(_voxelRoot->get_count());

    // std::vector<int> uniqueLoDValues;
    // _voxelRoot->populateUniqueLoDValues(uniqueLoDValues);
    // printUniqueLoDValues(uniqueLoDValues);
}

void JarVoxelTerrain::process_chunk_queue(float delta)
{
    if (_updateChunkCollidersQueue.empty())
        return;

    int rate = std::max(1, static_cast<int>(std::ceil(_updatedCollidersPerSecond * delta)));
    int target = std::min(rate, (int)_updateChunkCollidersQueue.size());

    int processed = 0;
    while (processed < target && !_updateChunkCollidersQueue.empty())
    {
        VoxelOctreeNode *node = _updateChunkCollidersQueue.front();
        _updateChunkCollidersQueue.pop();
        if (node == nullptr)
            continue;
        JarVoxelChunk *chunk = node->get_chunk();
        if (chunk == nullptr)
            continue;

        if (JarVoxelChunk *chunk = node->get_chunk())
        {
            chunk->update_collision_mesh();
            processed++;
        }
    }
}

void JarVoxelTerrain::generate_epsilons()
{
    int numElements = _size + 1;

    std::vector<int> sizes(numElements);
    for (int i = 0; i < numElements; i++)
    {
        sizes[i] = 1 << i;
    }

    _voxelEpsilons.resize(sizes.size());
    for (int i = 0; i < sizes.size(); i++)
    {
        int size = sizes[i];
        float x = size * _octreeScale;
        _voxelEpsilons[i] = 1.75f * x;
    }
}

void JarVoxelTerrain::process_modify_queue()
{
    if (_isBuilding)
        return;
    _isBuilding = true;
    // std::thread([this]() {
    if (!_modifySettingsQueue.empty())
    {
        auto &settings = _modifySettingsQueue.front();
        _modifySettingsQueue.pop();
        _voxelRoot->modify_sdf_in_bounds(*this, settings);
        //_populationRoot->remove_population(settings);
    }
    _isBuilding = false;
    // }).detach();
}

// void JarVoxelTerrain::process_delete_chunk_queue()
// {
//     if (_isBuilding)
//         return;
//     while (!_deleteChunkQueue.empty()) {
//         auto node = _deleteChunkQueue.front();
//         _deleteChunkQueue.pop();

//         if(node.is_parent_enqueued() || node.is_enqueued() || node.is_any_children_enqueued()) continue;
//         //delete and check if parents/children should be deleted.
//     }
// }

void JarVoxelTerrain::get_voxel_leaves_in_bounds(const Bounds &bounds, std::vector<VoxelOctreeNode *> &nodes) const
{
    _voxelRoot->get_voxel_leaves_in_bounds(*this, bounds, nodes);
}

void JarVoxelTerrain::get_voxel_leaves_in_bounds(const Bounds &bounds, int lod,
                                                 std::vector<VoxelOctreeNode *> &nodes) const
{
    _voxelRoot->get_voxel_leaves_in_bounds(*this, bounds, lod, nodes);
}

void JarVoxelTerrain::get_voxel_leaves_in_bounds_excluding_bounds(const Bounds &bounds, const Bounds &excludeBounds,
                                                                  int lod, std::vector<VoxelOctreeNode *> &nodes) const
{
    _voxelRoot->get_voxel_leaves_in_bounds_excluding_bounds(*this, bounds, excludeBounds, lod, nodes);
}

void JarVoxelTerrain::spawn_debug_spheres_in_bounds(const Vector3 &position, const float range)
{
    std::vector<VoxelOctreeNode *> nodes;
    auto center = glm::vec3(position.x, position.y, position.z);
    auto bounds = Bounds(center - range, center + range);
    get_voxel_leaves_in_bounds(bounds, nodes);

    Ref<StandardMaterial3D> red_material;
    red_material.instantiate();
    red_material->set_albedo(Color(1, 0, 0));
    Ref<StandardMaterial3D> green_material;
    green_material.instantiate();
    green_material->set_albedo(Color(0, 1, 0));

    Ref<SphereMesh> sphere_mesh;
    sphere_mesh.instantiate();
    sphere_mesh->set_radius(0.1f);
    sphere_mesh->set_height(0.2f);

    for (auto &&n : nodes)
    {
        Vector3 nodeCenter(n->_center.x, n->_center.y, n->_center.z);

        MeshInstance3D *sphereInstance = memnew(MeshInstance3D);
        add_child(sphereInstance);

        sphereInstance->set_mesh(sphere_mesh);
        sphereInstance->set_position(nodeCenter);
        sphereInstance->set_material_override((n->get_value() > 0) ? green_material : red_material);
    }
}

void JarVoxelTerrain::force_update_lod()
{
    if (_voxelLod.update_camera_position(*this, true))
        build();
}

glm::vec3 JarVoxelTerrain::get_camera_position() const
{
    return _voxelLod.get_camera_position();
}

int JarVoxelTerrain::desired_lod(const VoxelOctreeNode &node)
{
    return _voxelLod.desired_lod(node);
}

int JarVoxelTerrain::lod_at(const glm::vec3 &position) const
{
    return _voxelLod.lod_at(position);
}

void JarVoxelTerrain::set_terrain_details(const TypedArray<JarTerrainDetail> &details)
{
    _terrainDetails = details;
}

TypedArray<JarTerrainDetail> JarVoxelTerrain::get_terrain_details() const
{
    return _terrainDetails;
}
