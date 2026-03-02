#include "voxel_terrain.h"
#include "plane_sdf.h"
#include "sphere_sdf.h"
#include "world.h"

using namespace godot;

void JarVoxelTerrain::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_player_node"), &JarVoxelTerrain::get_player_node);
    ClassDB::bind_method(D_METHOD("set_player_node", "player_node"), &JarVoxelTerrain::set_player_node);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "player_node", PROPERTY_HINT_NODE_TYPE, "Node3D"), "set_player_node",
                 "get_player_node");

    ClassDB::bind_method(D_METHOD("get_octree_scale"), &JarVoxelTerrain::get_octree_scale);
    ClassDB::bind_method(D_METHOD("set_octree_scale", "value"), &JarVoxelTerrain::set_octree_scale);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "octree_scale"), "set_octree_scale", "get_octree_scale");

    ClassDB::bind_method(D_METHOD("get_size"), &JarVoxelTerrain::get_size);
    ClassDB::bind_method(D_METHOD("set_size", "value"), &JarVoxelTerrain::set_size);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "size"), "set_size", "get_size");

    ClassDB::bind_method(D_METHOD("get_chunk_size_log2"), &JarVoxelTerrain::get_chunk_size_log2);
    ClassDB::bind_method(D_METHOD("set_chunk_size_log2", "value"), &JarVoxelTerrain::set_chunk_size_log2);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "chunk_size_log2"), "set_chunk_size_log2", "get_chunk_size_log2");

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

    // -------------------------------------------------- MATERIALS --------------------------------------------------
    ADD_GROUP("Materials", "materials_");
    ClassDB::bind_method(D_METHOD("get_material_mode"), &JarVoxelTerrain::get_material_mode);
    ClassDB::bind_method(D_METHOD("set_material_mode", "mode"), &JarVoxelTerrain::set_material_mode);
    BIND_ENUM_CONSTANT(VOXEL_MATERIAL_MODE_DISCRETE_CHANNEL_SPLATTING);
    BIND_ENUM_CONSTANT(VOXEL_MATERIAL_MODE_MULTIPLE_MESHES);
    BIND_ENUM_CONSTANT(VOXEL_MATERIAL_MODE_PACKED_COLOR);
    BIND_ENUM_CONSTANT(VOXEL_MATERIAL_MODE_INDEXED_BLEND_SPLATTING);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "materials_material_mode", PROPERTY_HINT_ENUM,
                              "DiscreteChannelSplatting,MultipleMeshes,PackedColor,IndexedBlendSplatting"),
                 "set_material_mode", "get_material_mode");
    ClassDB::bind_method(D_METHOD("get_materials"), &JarVoxelTerrain::get_materials);
    ClassDB::bind_method(D_METHOD("set_materials", "materials"), &JarVoxelTerrain::set_materials);
    ADD_PROPERTY(
        PropertyInfo(Variant::ARRAY, "materials", PROPERTY_HINT_TYPE_STRING,
                     String::num(Variant::OBJECT) + "/" + String::num(PROPERTY_HINT_RESOURCE_TYPE) + ":Material"),
        "set_materials", "get_materials");

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

    ClassDB::bind_method(D_METHOD("get_collider_max_lod_threshold"), &JarVoxelTerrain::get_collider_max_lod_threshold);
    ClassDB::bind_method(D_METHOD("set_collider_max_lod_threshold", "value"),
                         &JarVoxelTerrain::set_collider_max_lod_threshold);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "performance_collider_max_lod_threshold"), "set_collider_max_lod_threshold",
                 "get_collider_max_lod_threshold");

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

    ClassDB::bind_method(D_METHOD("modify_using_sdf", "sdf_modification"), &JarVoxelTerrain::modify_using_sdf);

    ClassDB::bind_method(D_METHOD("spawn_debug_spheres_in_bounds", "position", "range"),
                         &JarVoxelTerrain::spawn_debug_spheres_in_bounds);
    ClassDB::bind_method(D_METHOD("force_update_lod"), &JarVoxelTerrain::force_update_lod);
}

JarVoxelTerrain::JarVoxelTerrain() : _octreeScale(1.0f), _size(14), _playerNode(nullptr)
{
    _chunkSize = (1 << _chunk_size_log2);
}

JarVoxelTerrain::~JarVoxelTerrain()
{
    if (_buildThread.joinable())
    {
        _buildThread.join();
    }
    _meshComputeScheduler.reset();
}

void JarVoxelTerrain::modify_using_sdf(const Ref<JarSdfModification> sdf)
{
    if (!sdf.is_valid())
        return;

    auto godot_global_position = get_global_position();
    glm::vec3 global_position = glm::vec3(godot_global_position.x, godot_global_position.y, godot_global_position.z);

    ModifySettings settings = sdf->to_settings(global_position, _octreeScale * 2.0f);
    _voxelRoot->modify_sdf_in_bounds(*this, settings);
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
    return _world;
}

void JarVoxelTerrain::set_world_node(JarWorld *worldNode)
{
    _world = worldNode;
}

bool JarVoxelTerrain::is_building() const
{
    return _isBuilding.load(std::memory_order_acquire);
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

int JarVoxelTerrain::get_chunk_size_log2() const
{
    return _chunk_size_log2;
}

void JarVoxelTerrain::set_chunk_size_log2(int value)
{
    _chunk_size_log2 = value;
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

VoxelMaterialMode godot::JarVoxelTerrain::get_material_mode() const
{
    return _materialMode;
}

void godot::JarVoxelTerrain::set_material_mode(VoxelMaterialMode mode)
{
    _materialMode = mode;
}

TypedArray<Material> JarVoxelTerrain::get_materials() const
{
    return _materials;
}

void godot::JarVoxelTerrain::set_materials(const TypedArray<Material> &materials)
{
    _materials = materials;
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

int godot::JarVoxelTerrain::get_collider_max_lod_threshold() const
{
    return _collider_max_lod_threshold;
}

void godot::JarVoxelTerrain::set_collider_max_lod_threshold(int value)
{
    _collider_max_lod_threshold = value;
}

int JarVoxelTerrain::get_lod_level_count() const
{
    return _lod_level_count;
}

void JarVoxelTerrain::set_lod_level_count(int value)
{
    _lod_level_count = value;
}

int JarVoxelTerrain::get_lod_shell_size() const
{
    return _lod_shell_size;
}

void JarVoxelTerrain::set_lod_shell_size(int value)
{
    _lod_shell_size = value;
}

bool JarVoxelTerrain::get_lod_automatic_update() const
{
    return _lod_automatic_update;
}

void JarVoxelTerrain::set_lod_automatic_update(bool value)
{
    _lod_automatic_update = value;
}

float JarVoxelTerrain::get_lod_automatic_update_distance() const
{
    return _lod_automatic_update_distance;
}

void JarVoxelTerrain::set_lod_automatic_update_distance(float value)
{
    _lod_automatic_update_distance = value;
}

void JarVoxelTerrain::_notification(int p_what)
{
    if (godot::Engine::get_singleton()->is_editor_hint())
    {
        return;
    }
    switch (p_what)
    {
    case NOTIFICATION_ENTER_TREE:
    {
        initialize();
        set_process_internal(true);
        break;
    }
    case NOTIFICATION_READY:
    {
        break;
    }
    case NOTIFICATION_EXIT_TREE:
    {
        set_process_internal(false);
        if (_buildThread.joinable())
        {
            _buildThread.join();
        }
        _isBuilding.store(false, std::memory_order_release);
        break;
    }
    case NOTIFICATION_INTERNAL_PROCESS:
    {
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
    _chunkSize = (1 << _chunk_size_log2);
    _voxelLod = JarVoxelLoD(_lod_automatic_update, _lod_automatic_update_distance, _lod_level_count, _lod_shell_size,
                            _octreeScale, _chunkSize);
    _meshComputeScheduler = std::make_unique<MeshComputeScheduler>(_maxConcurrentTasks);
    _voxelRoot = std::make_unique<VoxelOctreeNode>(_size);
    //_populationRoot = memnew(PopulationOctreeNode(_size));
    build();
}

void JarVoxelTerrain::process()
{
    float delta = get_process_delta_time();
    if (!_meshComputeScheduler)
        return;

    if (!is_building() && !_meshComputeScheduler->is_meshing() && _voxelLod.process(*this, false))
        build();
    _meshComputeScheduler->process(*this);

    if (!_modifySettingsQueue.empty())
    {
        process_modify_queue();
    }

    // UtilityFunctions::print("updating");

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
    // UtilityFunctions::print(lodString);
}

void JarVoxelTerrain::build()
{
    if (is_building() || _meshComputeScheduler->is_meshing())
        return;

    if (_buildThread.joinable())
    {
        _buildThread.join();
    }

    _isBuilding.store(true, std::memory_order_release);
    _buildThread = std::thread([this]()
                               {
        _voxelRoot->build(*this);
        _isBuilding.store(false, std::memory_order_release); });

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
    if (is_building())
        return;
    _isBuilding.store(true, std::memory_order_release);
    // std::thread([this]() {
    if (!_modifySettingsQueue.empty())
    {
        auto &settings = _modifySettingsQueue.front();
        _modifySettingsQueue.pop();
        _voxelRoot->modify_sdf_in_bounds(*this, settings);
        //_populationRoot->remove_population(settings);
    }
    _isBuilding.store(false, std::memory_order_release);
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
        Vector3 nodeCenter(n->get_center().x, n->get_center().y, n->get_center().z);

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
