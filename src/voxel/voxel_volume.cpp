#include "voxel_volume.h"
#include "terrain_material_palette.h"
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include "modify_settings.h"
#include "plane_sdf.h"
#include "sphere_sdf.h"
#include "voxel_chunk.h"
#include "job_system.h"
#include <chrono>
#include <limits>

void JarVoxelVolume::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_voxel_size"), &JarVoxelVolume::get_voxel_size);
    ClassDB::bind_method(D_METHOD("set_voxel_size", "meters"), &JarVoxelVolume::set_voxel_size);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "voxel_size", PROPERTY_HINT_RANGE, "0.05,16,0.01,or_greater,suffix:m"),
                 "set_voxel_size", "get_voxel_size");

    ClassDB::bind_method(D_METHOD("get_extent"), &JarVoxelVolume::get_extent);
    ClassDB::bind_method(D_METHOD("set_extent", "meters"), &JarVoxelVolume::set_extent);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "extent", PROPERTY_HINT_RANGE, "64,1048576,1,or_greater,exp,suffix:m"),
                 "set_extent", "get_extent");

    ClassDB::bind_method(D_METHOD("get_chunk_scene"), &JarVoxelVolume::get_chunk_scene);
    ClassDB::bind_method(D_METHOD("set_chunk_scene", "value"), &JarVoxelVolume::set_chunk_scene);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "chunk_scene", PROPERTY_HINT_RESOURCE_TYPE, "PackedScene"),
                 "set_chunk_scene", "get_chunk_scene");
    ADD_GROUP("Materials", "");
    BIND_ENUM_CONSTANT(MATERIAL_MODE_WEIGHTS);
    BIND_ENUM_CONSTANT(MATERIAL_MODE_SPLIT);
    BIND_ENUM_CONSTANT(MATERIAL_MODE_INDEXED);
    ClassDB::bind_method(D_METHOD("get_material_mode"), &JarVoxelVolume::get_material_mode);
    ClassDB::bind_method(D_METHOD("set_material_mode", "mode"), &JarVoxelVolume::set_material_mode);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "material_mode", PROPERTY_HINT_ENUM, "Weights,Split,Indexed"),
                 "set_material_mode", "get_material_mode");
    ClassDB::bind_method(D_METHOD("get_terrain_materials"), &JarVoxelVolume::get_terrain_materials);
    ClassDB::bind_method(D_METHOD("set_terrain_materials", "materials"), &JarVoxelVolume::set_terrain_materials);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "materials", PROPERTY_HINT_TYPE_STRING,
                              String::num(Variant::OBJECT) + "/" + String::num(PROPERTY_HINT_RESOURCE_TYPE) +
                                  ":JarTerrainMaterial"),
                 "set_terrain_materials", "get_terrain_materials");
    ClassDB::bind_method(D_METHOD("get_material"), &JarVoxelVolume::get_material);
    ClassDB::bind_method(D_METHOD("set_material", "value"), &JarVoxelVolume::set_material);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE,
                              "ShaderMaterial,StandardMaterial3D,ORMMaterial3D"),
                 "set_material", "get_material");
    ClassDB::bind_method(D_METHOD("get_runtime_material"), &JarVoxelVolume::get_runtime_material);
    ClassDB::bind_method(D_METHOD("update_materials"), &JarVoxelVolume::update_materials);
    ADD_GROUP("", "");

    ClassDB::bind_method(D_METHOD("get_sdf"), &JarVoxelVolume::get_sdf);
    ClassDB::bind_method(D_METHOD("set_sdf", "sdf"), &JarVoxelVolume::set_sdf);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sdf", PROPERTY_HINT_RESOURCE_TYPE, "JarSignedDistanceField"), "set_sdf",
                 "get_sdf");

    ClassDB::bind_method(D_METHOD("get_cubic_voxels"), &JarVoxelVolume::get_cubic_voxels);
    ClassDB::bind_method(D_METHOD("set_cubic_voxels", "cubic_voxels"), &JarVoxelVolume::set_cubic_voxels);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cubic_voxels"), "set_cubic_voxels", "get_cubic_voxels");

    ADD_GROUP("Meshing", "");
    BIND_ENUM_CONSTANT(MESHING_SURFACE_NETS);
    BIND_ENUM_CONSTANT(MESHING_DUAL_CONTOURING);
    ClassDB::bind_method(D_METHOD("get_meshing_algorithm"), &JarVoxelVolume::get_meshing_algorithm);
    ClassDB::bind_method(D_METHOD("set_meshing_algorithm", "algorithm"), &JarVoxelVolume::set_meshing_algorithm);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "meshing_algorithm", PROPERTY_HINT_ENUM, "Surface Nets,Dual Contouring"),
                 "set_meshing_algorithm", "get_meshing_algorithm");
    ClassDB::bind_method(D_METHOD("get_dual_contouring_feature_threshold"),
                         &JarVoxelVolume::get_dual_contouring_feature_threshold);
    ClassDB::bind_method(D_METHOD("set_dual_contouring_feature_threshold", "value"),
                         &JarVoxelVolume::set_dual_contouring_feature_threshold);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dual_contouring_feature_threshold", PROPERTY_HINT_RANGE, "0.001,1,0.001"),
                 "set_dual_contouring_feature_threshold", "get_dual_contouring_feature_threshold");
    ClassDB::bind_method(D_METHOD("get_manifold"), &JarVoxelVolume::get_manifold);
    ClassDB::bind_method(D_METHOD("set_manifold", "enabled"), &JarVoxelVolume::set_manifold);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "manifold"), "set_manifold", "get_manifold");
    ClassDB::bind_method(D_METHOD("remesh_all"), &JarVoxelVolume::remesh_all);

    // -------------------------------------------------- PERFORMANCE --------------------------------------------------
    ADD_GROUP("Performance", "performance_");
    ClassDB::bind_method(D_METHOD("get_updated_colliders_per_second"),
                         &JarVoxelVolume::get_updated_colliders_per_second);
    ClassDB::bind_method(D_METHOD("set_updated_colliders_per_second", "value"),
                         &JarVoxelVolume::set_updated_colliders_per_second);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "performance_updated_colliders_per_second"),
                 "set_updated_colliders_per_second", "get_updated_colliders_per_second");

    ClassDB::bind_method(D_METHOD("get_collider_budget_ms"), &JarVoxelVolume::get_collider_budget_ms);
    ClassDB::bind_method(D_METHOD("set_collider_budget_ms", "value"), &JarVoxelVolume::set_collider_budget_ms);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "performance_collider_budget_ms", PROPERTY_HINT_RANGE, "0,16,0.1"),
                 "set_collider_budget_ms", "get_collider_budget_ms");

    ADD_GROUP("Collision", "collision_");
    ClassDB::bind_method(D_METHOD("get_collision_layer"), &JarVoxelVolume::get_collision_layer);
    ClassDB::bind_method(D_METHOD("set_collision_layer", "layer"), &JarVoxelVolume::set_collision_layer);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_layer",
                 "get_collision_layer");
    ClassDB::bind_method(D_METHOD("get_collision_mask"), &JarVoxelVolume::get_collision_mask);
    ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &JarVoxelVolume::set_collision_mask);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask",
                 "get_collision_mask");
    ADD_GROUP("Performance", "performance_");
    ClassDB::bind_method(D_METHOD("get_collider_distance"), &JarVoxelVolume::get_collider_distance);
    ClassDB::bind_method(D_METHOD("set_collider_distance", "value"), &JarVoxelVolume::set_collider_distance);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "performance_collider_distance", PROPERTY_HINT_RANGE, "0,4096,1,or_greater"),
                 "set_collider_distance", "get_collider_distance");

    // -------------------------------------------------- LOD --------------------------------------------------
    ADD_GROUP("Level Of Detail", "lod_");
    ClassDB::bind_method(D_METHOD("get_lod_detail_distance"), &JarVoxelVolume::get_lod_detail_distance);
    ClassDB::bind_method(D_METHOD("set_lod_detail_distance", "meters"), &JarVoxelVolume::set_lod_detail_distance);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_detail_distance", PROPERTY_HINT_RANGE, "0,1024,1,or_greater,suffix:m"),
                 "set_lod_detail_distance", "get_lod_detail_distance");

    ClassDB::bind_method(D_METHOD("get_lod_shell_size"), &JarVoxelVolume::get_lod_shell_size);
    ClassDB::bind_method(D_METHOD("set_lod_shell_size", "value"), &JarVoxelVolume::set_lod_shell_size);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "lod_shell_size", PROPERTY_HINT_RANGE, "1,8,1"), "set_lod_shell_size",
                 "get_lod_shell_size");

    ClassDB::bind_method(D_METHOD("get_lod_automatic_update"), &JarVoxelVolume::get_lod_automatic_update);
    ClassDB::bind_method(D_METHOD("set_lod_automatic_update", "value"), &JarVoxelVolume::set_lod_automatic_update);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "lod_automatic_update"), "set_lod_automatic_update",
                 "get_lod_automatic_update");

    ClassDB::bind_method(D_METHOD("get_lod_automatic_update_distance"),
                         &JarVoxelVolume::get_lod_automatic_update_distance);
    ClassDB::bind_method(D_METHOD("set_lod_automatic_update_distance", "value"),
                         &JarVoxelVolume::set_lod_automatic_update_distance);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "lod_automatic_update_distance"), "set_lod_automatic_update_distance",
                 "get_lod_automatic_update_distance");

    //-------------------------------------------------- UP / HEIGHT --------------------------------------------------
    ADD_GROUP("Up Direction", "");
    BIND_ENUM_CONSTANT(UP_MODE_AXIS);
    BIND_ENUM_CONSTANT(UP_MODE_RADIAL);
    BIND_ENUM_CONSTANT(UP_MODE_AUTO);
    ClassDB::bind_method(D_METHOD("get_up_field_info"), &JarVoxelVolume::get_up_field_info);
    ClassDB::bind_method(D_METHOD("get_up_mode"), &JarVoxelVolume::get_up_mode);
    ClassDB::bind_method(D_METHOD("set_up_mode", "mode"), &JarVoxelVolume::set_up_mode);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "up_mode", PROPERTY_HINT_ENUM, "Axis,Radial,Auto"), "set_up_mode", "get_up_mode");
    ClassDB::bind_method(D_METHOD("get_up_axis"), &JarVoxelVolume::get_up_axis);
    ClassDB::bind_method(D_METHOD("set_up_axis", "axis"), &JarVoxelVolume::set_up_axis);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "up_axis"), "set_up_axis", "get_up_axis");
    ClassDB::bind_method(D_METHOD("get_up_center"), &JarVoxelVolume::get_up_center);
    ClassDB::bind_method(D_METHOD("set_up_center", "center"), &JarVoxelVolume::set_up_center);
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "up_center"), "set_up_center", "get_up_center");
    ClassDB::bind_method(D_METHOD("get_sea_level"), &JarVoxelVolume::get_sea_level);
    ClassDB::bind_method(D_METHOD("set_sea_level", "value"), &JarVoxelVolume::set_sea_level);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sea_level"), "set_sea_level", "get_sea_level");
    ClassDB::bind_method(D_METHOD("get_up_direction_at", "global_position"), &JarVoxelVolume::get_up_direction_at);
    ClassDB::bind_method(D_METHOD("get_height_at", "global_position"), &JarVoxelVolume::get_height_at);
    ClassDB::bind_method(D_METHOD("raycast_sdf", "from", "to"), &JarVoxelVolume::raycast_sdf);

    //-------------------------------------------------- POPULATION --------------------------------------------------
    ADD_GROUP("Population", "population_");
    ClassDB::bind_method(D_METHOD("get_terrain_features"), &JarVoxelVolume::get_terrain_features);
    ClassDB::bind_method(D_METHOD("set_terrain_features", "value"), &JarVoxelVolume::set_terrain_features);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "population_terrain_features", PROPERTY_HINT_TYPE_STRING,
                              String::num(Variant::OBJECT) + "/" + String::num(PROPERTY_HINT_RESOURCE_TYPE) +
                                  ":JarTerrainFeature"),
                 "set_terrain_features", "get_terrain_features");
    ClassDB::bind_method(D_METHOD("get_feature_cell_size"), &JarVoxelVolume::get_feature_cell_size);
    ClassDB::bind_method(D_METHOD("set_feature_cell_size", "value"), &JarVoxelVolume::set_feature_cell_size);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "population_feature_cell_size", PROPERTY_HINT_RANGE,
                              "8,512,1,or_greater,suffix:m"),
                 "set_feature_cell_size", "get_feature_cell_size");
    ClassDB::bind_method(D_METHOD("remove_features", "global_position", "radius"), &JarVoxelVolume::remove_features);
    ClassDB::bind_method(D_METHOD("get_features_near", "global_position", "radius"),
                         &JarVoxelVolume::get_features_near);
    ClassDB::bind_method(D_METHOD("regenerate_population"), &JarVoxelVolume::regenerate_population);
    ClassDB::bind_method(D_METHOD("get_biome_map"), &JarVoxelVolume::get_biome_map);
    ClassDB::bind_method(D_METHOD("set_biome_map", "value"), &JarVoxelVolume::set_biome_map);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "population_biome_map", PROPERTY_HINT_RESOURCE_TYPE, "JarBiomeMap"),
                 "set_biome_map", "get_biome_map");
    ClassDB::bind_method(D_METHOD("get_biome_weights_at", "global_position"), &JarVoxelVolume::get_biome_weights_at);
    ClassDB::bind_method(D_METHOD("get_terrain_details"), &JarVoxelVolume::get_terrain_details);
    ClassDB::bind_method(D_METHOD("set_terrain_details", "value"), &JarVoxelVolume::set_terrain_details);
    ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "population_terrain_details", PROPERTY_HINT_TYPE_STRING,
                              String::num(Variant::OBJECT) + "/" + String::num(PROPERTY_HINT_RESOURCE_TYPE) +
                                  ":JarTerrainDetail"),
                 "set_terrain_details", "get_terrain_details");

    BIND_ENUM_CONSTANT(SDF_OPERATION_UNION);
    BIND_ENUM_CONSTANT(SDF_OPERATION_SUBTRACTION);
    BIND_ENUM_CONSTANT(SDF_OPERATION_INTERSECTION);
    BIND_ENUM_CONSTANT(SDF_OPERATION_SMOOTH_UNION);
    BIND_ENUM_CONSTANT(SDF_OPERATION_SMOOTH_SUBTRACTION);
    BIND_ENUM_CONSTANT(SDF_OPERATION_SMOOTH_INTERSECTION);
    ClassDB::bind_method(D_METHOD("edit", "sdf", "operation", "transform", "material", "smoothness"),
                         &JarVoxelVolume::edit, DEFVAL(-1), DEFVAL(0.0f));
    ClassDB::bind_method(D_METHOD("modify", "sdf", "operation", "position", "radius", "material"),
                         &JarVoxelVolume::modify, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("paint", "sdf", "transform", "material"), &JarVoxelVolume::paint);
    ClassDB::bind_method(D_METHOD("sphere_edit", "position", "radius", "union", "material"),
                         &JarVoxelVolume::sphere_edit, DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("spawn_debug_spheres_in_bounds", "position", "range"),
                         &JarVoxelVolume::spawn_debug_spheres_in_bounds);
    ClassDB::bind_method(D_METHOD("force_update_lod"), &JarVoxelVolume::force_update_lod);
    ClassDB::bind_method(D_METHOD("is_building"), &JarVoxelVolume::is_building);
    ClassDB::bind_method(D_METHOD("is_meshing"), &JarVoxelVolume::is_meshing);
}

JarVoxelVolume::JarVoxelVolume() = default;

JarVoxelVolume::~JarVoxelVolume()
{
    stop_build_worker();
    // Members are destroyed in reverse declaration order, which would free the
    // octree first. In-flight mesh tasks read the octree, so wait for them (by
    // destroying the scheduler) first.
    _featureManager.reset();
    _meshComputeScheduler.reset();
    _voxelRoot.reset();
}

// Persistent worker: sleeps until a build is requested, runs one octree build,
// then sleeps again. One thread for the terrain's whole lifetime instead of a
// fresh detached thread per build.
void JarVoxelVolume::build_worker()
{
    std::vector<ModifySettings> edits;
    for (;;)
    {
        bool rebuild;
        {
            std::unique_lock<std::mutex> lock(_buildMutex);
            _buildCv.wait(lock, [this] { return _buildRequested || !_workerEdits.empty() || _buildShutdown; });
            if (_buildShutdown)
                return;
            rebuild = _buildRequested;
            _buildRequested = false;
            edits.swap(_workerEdits);
        }
        if (_voxelRoot)
        {
            for (const ModifySettings &settings : edits)
                _voxelRoot->modify_sdf_in_bounds(*this, settings);
            if (rebuild)
                build_octree();
            // Settle averaged inner-node values now, so the mesh threads that run
            // next never lazily write to shared nodes.
            _voxelRoot->update_if_dirty();
        }
        edits.clear();
        _isBuilding = false;
    }
}

void JarVoxelVolume::build_octree()
{
    // The top of the tree is walked on this thread; subtrees below forkSize are
    // independent and built in parallel on the (idle) meshing pool.
    VoxelOctreeNode::BuildContext ctx;
    ctx.forkSize = ChunkSizeLog2 + 4;
    _voxelRoot->build(*this, &ctx);
    JobSystem::get_pool().parallel_for(
        ctx.subtrees.size(), [this, &ctx](size_t i) {
            ctx.subtrees[i]->build(*this);
            ctx.subtrees[i]->update_if_dirty(); // settle the subtree while it is hot in cache
        });
    for (VoxelOctreeNode *node : ctx.deferredPost)
        node->build_post(*this);
}

// Signal the worker to stop and JOIN it, so no build is in flight once the
// terrain (and its octree) start being destroyed. Fixes the shutdown SIGSEGV.
void JarVoxelVolume::stop_build_worker()
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
JarVoxelChunk *JarVoxelVolume::pool_acquire()
{
    if (!_freeChunks.empty())
    {
        JarVoxelChunk *c = _freeChunks.back();
        _freeChunks.pop_back();
        c->reactivate();
        return c;
    }
    JarVoxelChunk *c = static_cast<JarVoxelChunk *>(_chunkScene->instantiate());
    if (StaticBody3D *body = c->get_static_body())
    {
        body->set_collision_layer(_collisionLayer);
        body->set_collision_mask(_collisionMask);
    }
    add_child(c);
    return c;
}

void JarVoxelVolume::set_collision_layer(uint32_t value)
{
    _collisionLayer = value;
    for (int i = 0; i < get_child_count(); ++i)
        if (JarVoxelChunk *chunk = Object::cast_to<JarVoxelChunk>(get_child(i)))
            if (chunk->get_static_body())
                chunk->get_static_body()->set_collision_layer(value);
}

void JarVoxelVolume::set_collision_mask(uint32_t value)
{
    _collisionMask = value;
    for (int i = 0; i < get_child_count(); ++i)
        if (JarVoxelChunk *chunk = Object::cast_to<JarVoxelChunk>(get_child(i)))
            if (chunk->get_static_body())
                chunk->get_static_body()->set_collision_mask(value);
}

// Thread-safe (called from the build worker): just queue the chunk. The actual
// scene-tree work (hide/disable) happens on the main thread in process_pool().
void JarVoxelVolume::pool_release(JarVoxelChunk *chunk)
{
    if (chunk == nullptr)
        return;
    std::lock_guard<std::mutex> lock(_poolMutex);
    _releasedChunks.push_back(chunk);
}

// Main thread: park released chunks (hide + collision off) and pool them.
void JarVoxelVolume::process_pool()
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

void JarVoxelVolume::queue_edit(const Ref<JarSignedDistanceField> &sdf, SDF::Operation operation,
                                const Transform3D &transform, int material, float smoothness, const Bounds &limit,
                                bool paintOnly)
{
    if (sdf.is_null())
    {
        UtilityFunctions::push_error("JarVoxelVolume: edit needs an SDF.");
        return;
    }
    sdf->prepare(); // e.g. bake a mesh SDF, on this (main) thread

    // SDF space -> volume space.
    const Transform3D local = get_global_transform().affine_inverse() * transform;
    const glm::mat4 toVolume = to_glm(local);
    const Vector3 scale = local.basis.get_scale().abs();

    ModifySettings settings;
    settings.sdf = sdf;
    settings.toSdf = glm::inverse(toVolume);
    settings.distanceScale = std::max(float(std::min(scale.x, std::min(scale.y, scale.z))), 1e-4f);
    settings.operation = operation;
    settings.smoothness = smoothness;
    settings.material = material;
    settings.paintOnly = paintOnly;

    // Region the edit can change: the SDF's solid part (plus room for smooth
    // blending and the mesher's sampling margin), clipped to `limit`.
    const float margin = _voxelSize * 2.0f + std::max(0.0f, smoothness);
    Bounds region = sdf->bounds();
    if (region.is_valid() && !region.is_infinite())
        region = region.transformed(toVolume).expanded(margin);
    if (!limit.is_infinite())
        region = region.is_infinite() ? limit : region.intersected(limit);
    if (!region.is_valid())
        return; // nothing to change
    if (region.is_infinite())
    {
        UtilityFunctions::push_error("JarVoxelVolume: cannot edit with an unbounded SDF; use modify() with a radius.");
        return;
    }
    settings.bounds = region;
    // Features standing where the shape changes go away, for good.
    if (!paintOnly && _featureManager)
        _featureManager->remove_in(region);

    // Queued rather than applied immediately: the octree may only be mutated
    // when no build or meshing task is reading it (see process()).
    _modifySettingsQueue.push(settings);
}

void JarVoxelVolume::edit(const Ref<JarSignedDistanceField> &sdf, const Operation operation,
                          const Transform3D &transform, const int material, const float smoothness)
{
    queue_edit(sdf, static_cast<SDF::Operation>(operation), transform, material, smoothness);
}

void JarVoxelVolume::modify(const Ref<JarSignedDistanceField> sdf, const Operation operation,
                            const Vector3 &position, const float radius, const int material)
{
    // Use the caller's SDF centered on `position`, limited to `radius`. Falls
    // back to a sphere when none is given.
    Ref<JarSignedDistanceField> s = sdf;
    if (s.is_null())
    {
        Ref<JarSphereSdf> sphere;
        sphere.instantiate();
        sphere->set_radius(radius);
        s = sphere;
    }
    const Vector3 local = to_local(position);
    const glm::vec3 center(local.x, local.y, local.z);
    const glm::vec3 edge(radius + _voxelSize * 2.0f);
    queue_edit(s, static_cast<SDF::Operation>(operation), Transform3D(Basis(), position), material, 0.0f,
               Bounds(center - edge, center + edge));
}

void JarVoxelVolume::sphere_edit(const Vector3 &position, const float radius, bool operation_union,
                                 const int material)
{
    Ref<JarSphereSdf> sdf;
    sdf.instantiate();
    sdf->set_radius(radius);
    const auto operation =
        operation_union ? SDF::Operation::SDF_OPERATION_UNION : SDF::Operation::SDF_OPERATION_SUBTRACTION;
    queue_edit(sdf, operation, Transform3D(Basis(), position), material, 0.0f);
}

void JarVoxelVolume::paint(const Ref<JarSignedDistanceField> &sdf, const Transform3D &transform, const int material)
{
    queue_edit(sdf, SDF::Operation::SDF_OPERATION_UNION, transform, material, 0.0f, Bounds::infinite(), true);
}

void JarVoxelVolume::enqueue_chunk_collider(JarVoxelChunk *chunk)
{
    if (chunk == nullptr || chunk->collider_queued)
        return;
    chunk->collider_queued = true;
    _pendingColliders.push_back(chunk);
}

void JarVoxelVolume::enqueue_chunk_update(VoxelOctreeNode &node)
{
    _meshComputeScheduler->enqueue(node);
}

bool JarVoxelVolume::is_building() const
{
    return _isBuilding;
}

bool JarVoxelVolume::is_meshing() const
{
    return _meshComputeScheduler && _meshComputeScheduler->is_meshing();
}

Ref<JarSignedDistanceField> JarVoxelVolume::get_sdf() const
{
    return _sdf;
}
void JarVoxelVolume::set_sdf(const Ref<JarSignedDistanceField> &sdf)
{
    _sdf = sdf;
    apply_rendering_to_chunks(); // the up field may come from the SDF
}

// std:: MeshComputeScheduler JarVoxelVolume::get_mesh_scheduler() const
// {
//     return _meshComputeScheduler;
// }

float JarVoxelVolume::get_voxel_size() const
{
    return _voxelSize;
}

// Size settings shape the octree, so they apply when the volume initializes.
void JarVoxelVolume::set_voxel_size(float value)
{
    _voxelSize = std::max(0.001f, value);
}

float JarVoxelVolume::get_extent() const
{
    return _extent;
}

void JarVoxelVolume::set_extent(float value)
{
    _extent = std::max(1.0f, value);
}

Ref<PackedScene> JarVoxelVolume::get_chunk_scene() const
{
    return _chunkScene;
}

void JarVoxelVolume::set_chunk_scene(const Ref<PackedScene> &value)
{
    _chunkScene = value;
}

bool JarVoxelVolume::get_cubic_voxels() const
{
    return _cubicVoxels;
}

void JarVoxelVolume::set_cubic_voxels(bool value)
{
    _cubicVoxels = value;
    remesh_all();
}

void JarVoxelVolume::set_meshing_algorithm(MeshingAlgorithm value)
{
    _meshingAlgorithm = value;
    notify_property_list_changed();
    remesh_all();
}

void JarVoxelVolume::set_dual_contouring_feature_threshold(float value)
{
    _dcFeatureThreshold = std::clamp(value, 0.001f, 1.0f);
    if (_meshingAlgorithm == MESHING_DUAL_CONTOURING)
        remesh_all();
}

void JarVoxelVolume::set_manifold(bool value)
{
    _manifold = value;
    remesh_all();
}

void JarVoxelVolume::remesh_all()
{
    // Mesh tasks read these settings, so changes only take effect through a new
    // build, which runs once no mesh task is in flight.
    _meshVersion++;
    _rebuildRequested = true;
}

int JarVoxelVolume::get_updated_colliders_per_second() const
{
    return _updatedCollidersPerSecond;
}
void JarVoxelVolume::set_updated_colliders_per_second(int value)
{
    _updatedCollidersPerSecond = value;
}
float JarVoxelVolume::get_collider_distance() const
{
    return _colliderDistance;
}
void JarVoxelVolume::set_collider_distance(float value)
{
    _colliderDistance = value;
}
float JarVoxelVolume::get_collider_budget_ms() const
{
    return _colliderBudgetMs;
}
void JarVoxelVolume::set_collider_budget_ms(float value)
{
    _colliderBudgetMs = std::max(0.0f, value);
}
bool JarVoxelVolume::is_in_collider_range(const glm::vec3 &pos) const
{
    if (_colliderDistance <= 0.0f)
        return true; // disabled -> original behaviour (collider on every near-LOD chunk)
    return _voxelLod.distance_to_nearest_viewer(pos) <= _colliderDistance;
}

float JarVoxelVolume::get_lod_detail_distance() const
{
    return _lodDetailDistance;
}

void JarVoxelVolume::set_lod_detail_distance(float value)
{
    _lodDetailDistance = std::max(0.0f, value);
}

int JarVoxelVolume::get_lod_shell_size() const
{
    return _lod_shell_size;
}

void JarVoxelVolume::set_lod_shell_size(int value)
{
    _lod_shell_size = value;
}

bool JarVoxelVolume::get_lod_automatic_update() const
{
    return _lod_automatic_update;
}

void JarVoxelVolume::set_lod_automatic_update(bool value)
{
    _lod_automatic_update = value;
    notify_property_list_changed();
}

float JarVoxelVolume::get_lod_automatic_update_distance() const
{
    return _lod_automatic_update_distance;
}

void JarVoxelVolume::set_lod_automatic_update_distance(float value)
{
    _lod_automatic_update_distance = value;
}

void JarVoxelVolume::_notification(int p_what)
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

void JarVoxelVolume::initialize()
{
    // ENTER_TREE fires again when the terrain is re-parented. Rebuilding the
    // octree then would free it under the running build worker / mesh tasks.
    if (_voxelRoot)
        return;
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
    _sdf->prepare(); // main thread: caches the SDF needs before workers sample it
    // Smallest octree whose root (voxel_size * 2^depth) covers the extent, and
    // that is at least one chunk deep.
    _depth = std::max(ChunkSizeLog2 + 1, int(std::ceil(std::log2(_extent / _voxelSize))));
    _voxelLod = JarVoxelLoD(_lod_automatic_update, _lod_automatic_update_distance, _lod_shell_size,
                            get_chunk_size() * _voxelSize, _lodDetailDistance, _depth - ChunkSizeLog2);
    _meshComputeScheduler = std::make_unique<MeshComputeScheduler>();
    _voxelRoot = std::make_unique<VoxelOctreeNode>(_depth);
    _featureManager = std::make_unique<FeatureManager>(*this);
    rebuild_materials();
    //_populationRoot = memnew(PopulationOctreeNode(_size));
    if (!_buildThread.joinable())
        _buildThread = std::thread([this] { build_worker(); });
    // The first build happens in process(): the player node may not have entered
    // the tree yet, and building around the default camera position (the terrain
    // origin) wastes a full build + mesh pass that is thrown away immediately.
}

void JarVoxelVolume::process()
{
    // While a build thread owns the octree (_isBuilding), NO main-thread tree
    // access may run — meshing dispatch, modify queue, and chunk queue all touch
    // the same nodes the worker is mutating. Letting any of them run concurrently
    // was the heap-corruption / nondeterministic-SIGSEGV race. So bail the whole
    // frame until the build finishes.
    if (_materialsDirty && _voxelRoot)
        rebuild_materials();
    if (_featureManager)
        _featureManager->process(); // independent of the octree: runs during builds too
    if (_isBuilding || !_meshComputeScheduler)
        return; // building, or initialize() failed (missing chunk scene / sdf)
    process_pool(); // reclaim chunks the last build released, ready for reuse
    float delta = get_process_delta_time();

    if (!_initialBuildRequested)
    {
        _initialBuildRequested = true;
        _voxelLod.update_viewers(*this, true);
        build();
        return;
    }

    // Edits and LOD builds mutate the octree (including freeing nodes), so they
    // may only run once no mesh task holds node pointers. Edits take priority
    // so they are not starved by a moving camera.
    if (!_meshComputeScheduler->is_meshing())
    {
        if (!_modifySettingsQueue.empty())
            process_modify_queue();
        else if ((_rebuildRequested && _voxelLod.update_viewers(*this, true)) || _voxelLod.process(*this))
        {
            build(); // hands off to the build worker (sets _isBuilding); resume next frame
            return;
        }
    }
    _meshComputeScheduler->process(*this);

    process_chunk_queue(delta);
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

void JarVoxelVolume::build()
{
    // The octree build walks/mutates the shared tree on a worker thread. It must
    // NOT overlap (a) another build, or (b) the meshing pool that reads the same
    // nodes -> otherwise: heap corruption / nondeterministic SIGSEGV.
    //  - (a) is prevented by claiming _isBuilding atomically BEFORE spawning
    //        (was set inside the thread => TOCTOU window => 2nd racing build).
    //  - (b) is prevented by gating _meshComputeScheduler->process() on
    //        !_isBuilding in process(), so meshing only runs once a build is done.
    if (!_meshComputeScheduler || !_voxelRoot || _meshComputeScheduler->is_meshing())
        return;
    bool expected = false;
    if (!_isBuilding.compare_exchange_strong(expected, true))
        return; // a build is already in flight
    _rebuildRequested = false;

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

void JarVoxelVolume::process_chunk_queue(float delta)
{
    if (_pendingColliders.empty())
        return;

    // Uploading a collider (building the physics BVH) must happen on the main
    // thread, so spread it over frames: nearest chunks first, so the ground under
    // the player is solid soonest, until the per-frame time budget is used up.
    // performance_updated_colliders_per_second optionally caps it further (0 = off).
    std::vector<std::pair<float, JarVoxelChunk *>> byDistance;
    byDistance.reserve(_pendingColliders.size());
    for (JarVoxelChunk *chunk : _pendingColliders)
    {
        const Vector3 p = chunk->get_position();
        byDistance.emplace_back(_voxelLod.distance_to_nearest_viewer(glm::vec3(p.x, p.y, p.z)), chunk);
    }
    std::sort(byDistance.begin(), byDistance.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; }); // nearest last

    const int cap = _updatedCollidersPerSecond > 0
                        ? std::max(1, static_cast<int>(std::ceil(_updatedCollidersPerSecond * delta)))
                        : std::numeric_limits<int>::max();
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::microseconds(static_cast<int64_t>(_colliderBudgetMs * 1000.0f));
    int processed = 0;
    while (!byDistance.empty() && processed < cap)
    {
        // Chunks (not octree nodes) are queued: nodes can be freed by edits while
        // queued, whereas chunks are pooled and live as long as the terrain.
        JarVoxelChunk *chunk = byDistance.back().second;
        byDistance.pop_back();
        chunk->collider_queued = false;
        if (!chunk->is_visible())
            continue; // parked in the pool since it was queued
        chunk->update_collision_mesh();
        processed++;
        if (std::chrono::steady_clock::now() >= deadline)
            break;
    }

    _pendingColliders.clear();
    for (const auto &entry : byDistance)
        _pendingColliders.push_back(entry.second);
}

void JarVoxelVolume::process_modify_queue()
{
    // Edits run on the build worker, like builds, so a large edit never stalls
    // a frame. process() only calls this when no mesh task holds node pointers.
    bool expected = false;
    if (!_voxelRoot || !_isBuilding.compare_exchange_strong(expected, true))
        return;
    {
        std::lock_guard<std::mutex> lock(_buildMutex);
        while (!_modifySettingsQueue.empty())
        {
            _workerEdits.push_back(std::move(_modifySettingsQueue.front()));
            _modifySettingsQueue.pop();
        }
    }
    _buildCv.notify_one();
}

// void JarVoxelVolume::process_delete_chunk_queue()
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

void JarVoxelVolume::get_voxel_leaves_in_bounds(const Bounds &bounds, std::vector<VoxelOctreeNode *> &nodes) const
{
    _voxelRoot->get_voxel_leaves_in_bounds(*this, bounds, nodes);
}

void JarVoxelVolume::get_voxel_leaves_in_bounds(const Bounds &bounds, int lod,
                                                 std::vector<VoxelOctreeNode *> &nodes) const
{
    _voxelRoot->get_voxel_leaves_in_bounds(*this, bounds, lod, nodes);
}

void JarVoxelVolume::get_voxel_leaves_in_bounds_excluding_bounds(const Bounds &bounds, const Bounds &excludeBounds,
                                                                  int lod, std::vector<VoxelOctreeNode *> &nodes) const
{
    _voxelRoot->get_voxel_leaves_in_bounds_excluding_bounds(*this, bounds, excludeBounds, lod, nodes);
}

void JarVoxelVolume::spawn_debug_spheres_in_bounds(const Vector3 &position, const float range)
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

void JarVoxelVolume::force_update_lod()
{
    // Applied by the next build, which starts as soon as no mesh task runs
    // (calling build() directly is silently refused while meshing).
    _rebuildRequested = true;
}

float JarVoxelVolume::distance_to_nearest_viewer(const glm::vec3 &position) const
{
    return _voxelLod.distance_to_nearest_viewer(position);
}

JarVoxelVolume::UpMode JarVoxelVolume::get_up_mode() const
{
    return static_cast<UpMode>(_upMode);
}
void JarVoxelVolume::set_up_mode(UpMode mode)
{
    _upMode = mode;
    if (mode != UP_MODE_AUTO)
        _upField.mode = static_cast<UpField::Mode>(mode);
    notify_property_list_changed();
    apply_rendering_to_chunks();
}

UpField JarVoxelVolume::get_up_field() const
{
    UpField field = _upField;
    if (_upMode == UP_MODE_AUTO)
    {
        if (_sdf.is_null() || !_sdf->natural_up(field))
        {
            field = _upField;
            field.mode = UpField::AXIS;
        }
    }
    return field;
}

Dictionary JarVoxelVolume::get_up_field_info() const
{
    const UpField f = get_up_field();
    Dictionary d;
    d["mode"] = int(f.mode);
    d["axis"] = Vector3(f.axis.x, f.axis.y, f.axis.z);
    d["center"] = Vector3(f.center.x, f.center.y, f.center.z);
    d["sea_level"] = f.sea_level;
    return d;
}

void JarVoxelVolume::_validate_property(PropertyInfo &property) const
{
    const String name = property.name;
    bool hidden = false;
    if (name == "up_axis")
        hidden = _upMode != UP_MODE_AXIS;
    else if (name == "up_center" || name == "sea_level")
        hidden = _upMode == UP_MODE_AUTO;
    else if (name == "dual_contouring_feature_threshold")
        hidden = _meshingAlgorithm != MESHING_DUAL_CONTOURING;
    else if (name == "lod_automatic_update_distance")
        hidden = !_lod_automatic_update;
    if (hidden)
        property.usage = PROPERTY_USAGE_NO_EDITOR;
}
Vector3 JarVoxelVolume::get_up_axis() const
{
    return Vector3(_upField.axis.x, _upField.axis.y, _upField.axis.z);
}
void JarVoxelVolume::set_up_axis(const Vector3 &axis)
{
    const Vector3 n = axis.normalized();
    if (!n.is_zero_approx())
        _upField.axis = glm::vec3(n.x, n.y, n.z);
    apply_rendering_to_chunks();
}
Vector3 JarVoxelVolume::get_up_center() const
{
    return Vector3(_upField.center.x, _upField.center.y, _upField.center.z);
}
void JarVoxelVolume::set_up_center(const Vector3 &center)
{
    _upField.center = glm::vec3(center.x, center.y, center.z);
    apply_rendering_to_chunks();
}
float JarVoxelVolume::get_sea_level() const
{
    return _upField.sea_level;
}
void JarVoxelVolume::set_sea_level(float value)
{
    _upField.sea_level = value;
    apply_rendering_to_chunks();
}

void JarVoxelVolume::set_material(const Ref<Material> &value)
{
    _material = value;
    update_materials();
}

void JarVoxelVolume::set_material_mode(MaterialMode mode)
{
    _materialMode = mode;
    update_materials();
    remesh_all();
}

void JarVoxelVolume::set_terrain_materials(const TypedArray<JarTerrainMaterial> &materials)
{
    const Callable callback = callable_mp(this, &JarVoxelVolume::on_terrain_material_changed);
    for (int64_t i = 0; i < _terrainMaterials.size(); ++i)
    {
        Ref<JarTerrainMaterial> m = _terrainMaterials[i];
        if (m.is_valid() && m->is_connected("changed", callback))
            m->disconnect("changed", callback);
    }
    _terrainMaterials = materials;
    for (int64_t i = 0; i < _terrainMaterials.size(); ++i)
    {
        Ref<JarTerrainMaterial> m = _terrainMaterials[i];
        if (m.is_valid() && !m->is_connected("changed", callback))
            m->connect("changed", callback, Object::CONNECT_DEFERRED);
    }
    update_materials();
}

void JarVoxelVolume::on_terrain_material_changed()
{
    update_materials();
}

// The material chunks use: the user's (or the mode's default) material, copied
// per volume so each volume can have its own palette, with the palette, texture
// arrays and up field inputs filled in.
void JarVoxelVolume::update_materials()
{
    _materialsDirty = true;
}

void JarVoxelVolume::rebuild_materials()
{
    _materialsDirty = false;
    Ref<Material> base = _material;
    if (base.is_null())
    {
        static const char *Defaults[] = {
            "res://addons/jar_voxel_terrain/materials/terrain_weights.tres",
            "res://addons/jar_voxel_terrain/materials/terrain_split.tres",
            "res://addons/jar_voxel_terrain/materials/terrain_indexed.tres",
        };
        base = ResourceLoader::get_singleton()->load(Defaults[std::clamp(_materialMode.load(), 0, 2)]);
    }
    _runtimeMaterial = Ref<Material>();
    if (Ref<ShaderMaterial> shader = base; shader.is_valid())
    {
        Ref<ShaderMaterial> copy = shader->duplicate();
        TerrainMaterialPalette::apply(
            _terrainMaterials.is_empty() ? TerrainMaterialPalette::default_materials() : _terrainMaterials, _biomeMap,
            copy);
        _runtimeMaterial = copy;
    }
    else
        _runtimeMaterial = base;
    apply_rendering_to_chunks();
}

// Main thread. Chunks also apply these whenever they are (re)meshed.
void JarVoxelVolume::apply_rendering_to_chunks()
{
    if (!is_inside_tree())
        return;
    for (int i = 0; i < get_child_count(); ++i)
        if (JarVoxelChunk *chunk = Object::cast_to<JarVoxelChunk>(get_child(i)))
            if (chunk->get_mesh_instance() != nullptr)
                chunk->apply_rendering(*this);
}
Vector3 JarVoxelVolume::get_up_direction_at(const Vector3 &global_position) const
{
    const Vector3 l = to_local(global_position);
    const glm::vec3 up = get_up_field().up(glm::vec3(l.x, l.y, l.z));
    return get_global_basis().xform(Vector3(up.x, up.y, up.z)).normalized();
}
Dictionary JarVoxelVolume::raycast_sdf(const Vector3 &from, const Vector3 &to) const
{
    Dictionary result;
    if (_sdf.is_null())
        return result;
    const Vector3 lf = to_local(from), lt = to_local(to);
    const glm::vec3 origin(lf.x, lf.y, lf.z);
    glm::vec3 dir(lt.x - lf.x, lt.y - lf.y, lt.z - lf.z);
    const float length = glm::length(dir);
    if (length <= 0.0f)
        return result;
    dir /= length;
    // Sphere tracing; steps are shortened a little because noise-based fields
    // are not exact distances.
    const float epsilon = 0.01f * _voxelSize;
    float t = 0.0f;
    for (int i = 0; i < 1024 && t <= length; ++i)
    {
        const glm::vec3 p = origin + dir * t;
        const float d = _sdf->distance(p);
        if (d < epsilon)
        {
            const glm::vec3 n = _sdf->normal(p);
            result["position"] = to_global(Vector3(p.x, p.y, p.z));
            result["normal"] = get_global_basis().xform(Vector3(n.x, n.y, n.z)).normalized();
            return result;
        }
        t += std::max(d * 0.8f, epsilon);
    }
    return result;
}

float JarVoxelVolume::get_height_at(const Vector3 &global_position) const
{
    const Vector3 l = to_local(global_position);
    return get_up_field().height(glm::vec3(l.x, l.y, l.z));
}

int JarVoxelVolume::desired_lod(const VoxelOctreeNode &node)
{
    return _voxelLod.desired_lod(node);
}

int JarVoxelVolume::lod_at(const glm::vec3 &position) const
{
    return _voxelLod.lod_at(position);
}

std::vector<JarVoxelVolume::PopulationEntry<JarTerrainFeature>> JarVoxelVolume::population_features() const
{
    std::vector<PopulationEntry<JarTerrainFeature>> result;
    for (int64_t i = 0; i < _terrainFeatures.size(); ++i)
        result.push_back({_terrainFeatures[i], -1});
    if (_biomeMap.is_valid())
    {
        const TypedArray<JarBiome> biomes = _biomeMap->get_biomes();
        for (int64_t b = 0; b < biomes.size(); ++b)
        {
            Ref<JarBiome> biome = biomes[b];
            if (biome.is_null())
                continue;
            const TypedArray<JarTerrainFeature> features = biome->get_features();
            for (int64_t i = 0; i < features.size(); ++i)
                result.push_back({features[i], int(b)});
        }
    }
    return result;
}

std::vector<JarVoxelVolume::PopulationEntry<JarTerrainDetail>> JarVoxelVolume::population_details() const
{
    std::vector<PopulationEntry<JarTerrainDetail>> result;
    for (int64_t i = 0; i < _terrainDetails.size(); ++i)
        result.push_back({_terrainDetails[i], -1});
    if (_biomeMap.is_valid())
    {
        const TypedArray<JarBiome> biomes = _biomeMap->get_biomes();
        for (int64_t b = 0; b < biomes.size(); ++b)
        {
            Ref<JarBiome> biome = biomes[b];
            if (biome.is_null())
                continue;
            const TypedArray<JarTerrainDetail> details = biome->get_details();
            for (int64_t i = 0; i < details.size(); ++i)
                result.push_back({details[i], int(b)});
        }
    }
    return result;
}

void JarVoxelVolume::on_biome_map_changed()
{
    update_materials();
    regenerate_population();
}

void JarVoxelVolume::set_biome_map(const Ref<JarBiomeMap> &value)
{
    const Callable callback = callable_mp(this, &JarVoxelVolume::on_biome_map_changed);
    if (_biomeMap.is_valid() && _biomeMap->is_connected("changed", callback))
        _biomeMap->disconnect("changed", callback);
    _biomeMap = value;
    if (_biomeMap.is_valid())
        _biomeMap->connect("changed", callback, Object::CONNECT_DEFERRED);
    update_materials(); // biome ground materials live in the palette
    if (_featureManager)
        _featureManager->reset();
    remesh_all(); // per-vertex biome weights and details
}

Dictionary JarVoxelVolume::get_biome_weights_at(const Vector3 &global_position) const
{
    Dictionary result;
    if (_biomeMap.is_null())
        return result;
    const Vector3 l = to_local(global_position);
    const BiomeWeights w = _biomeMap->evaluate(glm::vec3(l.x, l.y, l.z), get_up_field());
    const TypedArray<JarBiome> biomes = _biomeMap->get_biomes();
    for (int i = 0; i < w.count; ++i)
        if (w.ids[i] < biomes.size())
            result[biomes[w.ids[i]]] = w.weights[i];
    return result;
}

void JarVoxelVolume::set_terrain_features(const TypedArray<JarTerrainFeature> &features)
{
    _terrainFeatures = features;
    if (_featureManager)
        _featureManager->reset();
}

void JarVoxelVolume::set_feature_cell_size(float value)
{
    _featureCellSize = std::max(1.0f, value);
    if (_featureManager)
        _featureManager->reset();
}

void JarVoxelVolume::regenerate_population()
{
    if (_featureManager)
        _featureManager->reset();
    remesh_all(); // details are made with the chunk meshes
}

void JarVoxelVolume::remove_features(const Vector3 &global_position, float radius)
{
    if (!_featureManager)
        return;
    const Vector3 local = to_local(global_position);
    const glm::vec3 c(local.x, local.y, local.z);
    _featureManager->remove_in(Bounds(c - glm::vec3(radius), c + glm::vec3(radius)));
}

TypedArray<Dictionary> JarVoxelVolume::get_features_near(const Vector3 &global_position, float radius) const
{
    TypedArray<Dictionary> result;
    if (!_featureManager)
        return result;
    const Vector3 local = to_local(global_position);
    for (const FeatureManager::Instance &instance :
         _featureManager->instances_near(glm::vec3(local.x, local.y, local.z), radius))
    {
        Dictionary d;
        d["feature"] = _featureManager->feature_at(instance.feature);
        d["transform"] = get_global_transform() * instance.transform;
        result.push_back(d);
    }
    return result;
}

void JarVoxelVolume::set_terrain_details(const TypedArray<JarTerrainDetail> &details)
{
    _terrainDetails = details;
}

TypedArray<JarTerrainDetail> JarVoxelVolume::get_terrain_details() const
{
    return _terrainDetails;
}
