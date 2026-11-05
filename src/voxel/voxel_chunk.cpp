#include "voxel_chunk.h"
#include "chunk_detail_generator.h"
#include "population_rendering.h"
#include "voxel_volume.h"
#include "voxel_octree_node.h"
#include "voxel_stats.h"
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

void JarVoxelChunk::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_mesh"), &JarVoxelChunk::get_array_mesh);
    ClassDB::bind_method(D_METHOD("get_mesh_instance"), &JarVoxelChunk::get_mesh_instance);
    ClassDB::bind_method(D_METHOD("set_mesh_instance", "mesh_instance"), &JarVoxelChunk::set_mesh_instance);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh_instance", PROPERTY_HINT_NODE_TYPE, "MeshInstance3D"),
                 "set_mesh_instance", "get_mesh_instance");

    ClassDB::bind_method(D_METHOD("get_center"), &JarVoxelChunk::get_center);
    ClassDB::bind_method(D_METHOD("get_collision_shape"), &JarVoxelChunk::get_collision_shape);
    ClassDB::bind_method(D_METHOD("set_collision_shape", "collision_shape"), &JarVoxelChunk::set_collision_shape);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "collision_shape", PROPERTY_HINT_NODE_TYPE, "CollisionShape3D"),
                 "set_collision_shape", "get_collision_shape");

    ClassDB::bind_method(D_METHOD("get_static_body"), &JarVoxelChunk::get_static_body);
    ClassDB::bind_method(D_METHOD("set_static_body", "static_body"), &JarVoxelChunk::set_static_body);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "static_body", PROPERTY_HINT_NODE_TYPE, "StaticBody3D"),
                 "set_static_body", "get_static_body");

    ClassDB::bind_method(D_METHOD("get_volume"), &JarVoxelChunk::get_volume);
    ClassDB::bind_method(D_METHOD("get_lod"), &JarVoxelChunk::get_lod);
    ClassDB::bind_method(D_METHOD("get_boundaries"), &JarVoxelChunk::get_boundaries);

    ClassDB::bind_method(D_METHOD("get_collider_lod_threshold"), &JarVoxelChunk::get_collider_lod_threshold);
    ClassDB::bind_method(D_METHOD("set_collider_lod_threshold", "collider_lod_threshold"),
                         &JarVoxelChunk::set_collider_lod_threshold);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collider_lod_threshold"), "set_collider_lod_threshold",
                 "get_collider_lod_threshold");
}

// At least n multimesh instances; unused ones stay (hidden) for reuse, since
// details come and go as the viewer moves.
void JarVoxelChunk::_update_multi_mesh_instances(int n)
{
    for (int i = int(multi_mesh_instances.size()); i < n; i++)
    {
        MultiMeshInstance3D *multi_mesh_instance = memnew(MultiMeshInstance3D);
        add_child(multi_mesh_instance);
        multi_mesh_instances.push_back(multi_mesh_instance);
        multi_mesh_instance->set_position(Vector3(0, 0, 0));
    }
}

JarVoxelChunk::JarVoxelChunk() : lod(0), edge_chunk(false)
{
}

JarVoxelChunk::~JarVoxelChunk()
{
}

int JarVoxelChunk::get_lod() const
{
    return lod;
}

void JarVoxelChunk::set_lod(int p_lod)
{
    lod = p_lod;
}

int JarVoxelChunk::get_collider_lod_threshold() const
{
    return collider_lod_threshold;
}

void JarVoxelChunk::set_collider_lod_threshold(int p_collider_lod_threshold)
{
    collider_lod_threshold = p_collider_lod_threshold;
}

uint64_t JarVoxelChunk::get_boundaries() const
{
    return boundaries;
}

void JarVoxelChunk::set_boundaries(uint64_t p_h2lboundaries)
{
    boundaries = p_h2lboundaries;
}

bool JarVoxelChunk::is_edge_chunk() const
{
    return edge_chunk;
}

void JarVoxelChunk::set_edge_chunk(bool p_edge_chunk)
{
    edge_chunk = p_edge_chunk;
}

MeshInstance3D *JarVoxelChunk::get_mesh_instance() const
{
    return mesh_instance;
}

void JarVoxelChunk::set_mesh_instance(MeshInstance3D *p_mesh_instance)
{
    mesh_instance = p_mesh_instance;
}

CollisionShape3D *JarVoxelChunk::get_collision_shape() const
{
    return collision_shape;
}

void JarVoxelChunk::set_collision_shape(CollisionShape3D *p_collision_shape)
{
    collision_shape = p_collision_shape;
}

StaticBody3D *JarVoxelChunk::get_static_body() const
{
    return static_body;
}

void JarVoxelChunk::set_static_body(StaticBody3D *p_static_body)
{
    static_body = p_static_body;
}

Ref<ArrayMesh> JarVoxelChunk::get_array_mesh() const
{
    return array_mesh;
}

void JarVoxelChunk::set_array_mesh(Ref<ArrayMesh> p_array_mesh)
{
    array_mesh = p_array_mesh;
}

Ref<ConcavePolygonShape3D> JarVoxelChunk::get_concave_polygon_shape() const
{
    return concave_polygon_shape;
}

void JarVoxelChunk::set_concave_polygon_shape(Ref<ConcavePolygonShape3D> p_concave_polygon_shape)
{
    concave_polygon_shape = p_concave_polygon_shape;
}

Ref<ShaderMaterial> JarVoxelChunk::get_material() const
{
    return material;
}

void JarVoxelChunk::set_material(Ref<ShaderMaterial> p_material)
{
    material = p_material;
}


void JarVoxelChunk::prepare_mesh(const JarVoxelVolume &terrain, const ChunkMeshData &chunk_mesh_data)
{
    const glm::vec3 position = chunk_mesh_data.bounds.get_center();
    set_position(Vector3(position.x, position.y, position.z));
    array_mesh = chunk_mesh_data.uploaded;
    // Straight to the RenderingServer: MeshInstance3D::set_mesh took ~10 us
    // (surface materials, notifications), half of showing thousands of
    // chunks in one commit. The node's own mesh property stays as the scene
    // has it; get_mesh() is the chunk's mesh.
    RenderingServer::get_singleton()->instance_set_base(mesh_instance->get_instance(), array_mesh->get_rid());
    apply_rendering(terrain);
}

void JarVoxelChunk::update_chunk(JarVoxelVolume &terrain, VoxelOctreeNode *node, ChunkMeshData *chunk_mesh_data)
{
    _chunk_mesh_data.reset(chunk_mesh_data);
    sample_gradients = chunk_mesh_data->gradients;
    if (concave_polygon_shape.is_null())
        concave_polygon_shape =
            Ref<ConcavePolygonShape3D>(Object::cast_to<ConcavePolygonShape3D>(*collision_shape->get_shape()));
    if (material.is_null())
        material = Ref<ShaderMaterial>(Object::cast_to<ShaderMaterial>(*mesh_instance->get_material_override()));
    lod = chunk_mesh_data->lod;
    boundaries = chunk_mesh_data->boundaries;
    mesh_version = chunk_mesh_data->mesh_version;
    edge_chunk = chunk_mesh_data->edge_chunk;
    auto old_bounds = bounds;
    bounds = chunk_mesh_data->bounds;
    // Vertices are relative to the chunk's center (on a half-meter grid), so
    // they keep their precision near the camera. Neighbors still place the
    // vertices they share bit for bit: with the floating origin the volume's
    // global transform is a plain translation on a fine grid (see
    // JarVoxelUniverse::shift_origin_if_far), so Godot adds the chunk centers
    // to it exactly. Turned or far from the origin, each chunk rounded its
    // place on its own: pinholes along the seams (a float step is 2 mm at
    // 16 km), which only show near the camera, where this holds.
    const glm::vec3 position = bounds.get_center();
    if (chunk_mesh_data->prepared == this)
        set_visible(true);
    else
    {
        if (chunk_mesh_data->uploaded.is_null())
            chunk_mesh_data->upload();
        prepare_mesh(terrain, *chunk_mesh_data);
    }

    // Details are made separately (DetailScheduler), near a viewer only, from
    // this mesh. Until new ones arrive the old ones stay (an edit nearby);
    // other LODs show none.
    detail_token++;
    detail_state = DETAILS_NONE;
    if (lod == 0)
    {
        detail_inputs.vertices = chunk_mesh_data->mesh_array[Mesh::ARRAY_VERTEX];
        detail_inputs.normals = chunk_mesh_data->mesh_array[Mesh::ARRAY_NORMAL];
        detail_inputs.indices = chunk_mesh_data->mesh_array[Mesh::ARRAY_INDEX];
        detail_inputs.painted = chunk_mesh_data->painted;
        detail_inputs.center = position;
        terrain.chunk_meshed(this);
    }
    else
    {
        detail_inputs = DetailInputs();
        terrain.chunk_gone(this);
    }

    // Colliders last: building one releases the mesh data (chunk_mesh_data),
    // which nothing above may then read.
    bool generate_collider = lod <= collider_lod_threshold && terrain.is_in_collider_range(position);
    if (generate_collider)
    {
        // The chunks this one replaces lose their colliders right now (see
        // VoxelOctreeNode::update_chunk), so near a viewer build the collider
        // immediately: a queued one leaves a gap a character can fall through.
        const float nearDistance = 2.0f * terrain.get_chunk_size() * terrain.get_voxel_size();
        if (terrain.distance_to_nearest_viewer(position) < nearDistance)
            update_collision_mesh();
        else
            terrain.enqueue_chunk_collider(this);
    }
    else
    {
        set_collider_active(false);
        // The CPU copy of the mesh is only needed for a collider that is still
        // to be built (update_collision_mesh releases it); the renderer has its own.
        terrain.discard_mesh_data(_chunk_mesh_data.release());
    }

    // Ref<StandardMaterial3D> stitch_material;
    // stitch_material.instantiate();
    // stitch_material->set_albedo(Color(1, 0, 1));
    // Ref<SphereMesh> sphere_mesh;
    // sphere_mesh.instantiate();
    // sphere_mesh->set_radius(0.4f);
    // sphere_mesh->set_height(0.8f);
    // PackedVector3Array verts = chunk_mesh_data.mesh_array[Mesh::ARRAY_VERTEX];
    // display all vertices:
    // for (auto& [position, vertexId]: chunk_mesh_data.edgeVertices)
    // {
    //     Vector3 nodeCenter = verts[vertexId];
    //     MeshInstance3D *sphereInstance = memnew(MeshInstance3D);
    //     add_child(sphereInstance);
    //     sphereInstance->set_mesh(sphere_mesh);
    //     sphereInstance->set_position(nodeCenter);
    //     sphereInstance->set_material_override(stitch_material);
    // }
}

// Shows details generated for this chunk. Two multimeshes per detail share
// its instances: the mesh near, the far mesh beyond far_distance.
void JarVoxelChunk::apply_details(const JarVoxelVolume &terrain, const std::vector<Ref<JarTerrainDetail>> &details,
                                  const std::vector<PackedFloat32Array> &buffers)
{
    const int detail_count = std::min<int>(buffers.size(), details.size());
    _update_multi_mesh_instances(2 * detail_count);
    const float margin = 0.87f * terrain.get_chunk_size() * terrain.get_voxel_size(); // half the chunk's diagonal
    for (int i = 0; i < int(multi_mesh_instances.size()); i++)
    {
        MultiMeshInstance3D *multi_mesh_instance = multi_mesh_instances[i];
        const Ref<JarTerrainDetail> detail = i < 2 * detail_count ? details[i / 2] : Ref<JarTerrainDetail>();
        const bool far = (i & 1) != 0;
        const bool has_far = detail.is_valid() && detail->get_far_mesh().is_valid() && detail->get_far_distance() > 0.0f;
        const Ref<Mesh> mesh = detail.is_null() ? Ref<Mesh>() : (far ? detail->get_far_mesh() : detail->get_mesh());
        if (mesh.is_null() || (far && !has_far) || buffers[i / 2].is_empty())
        {
            hide_multi_mesh_instance(multi_mesh_instance);
            continue;
        }
        const PackedFloat32Array &buffer = buffers[i / 2];
        multi_mesh_instance->set_visible(true);
        Ref<MultiMesh> multi_mesh = multi_mesh_instance->get_multimesh();
        if (multi_mesh.is_null())
        {
            multi_mesh.instantiate();
            multi_mesh->set_transform_format(MultiMesh::TRANSFORM_3D);
            multi_mesh->set_use_custom_data(true);
            multi_mesh_instance->set_multimesh(multi_mesh);
        }
        const float draw = detail->get_draw_distance();
        const float split = has_far ? detail->get_far_distance() : 0.0f;
        const float begin = far ? split : 0.0f, end = far || !has_far ? draw : split;
        // This level's range in each instance's custom data (y, z).
        PackedFloat32Array data = buffer;
        float *w = data.ptrw();
        for (int64_t k = 0; k + ChunkDetailGenerator::Stride <= data.size(); k += ChunkDetailGenerator::Stride)
        {
            w[k + 13] = begin;
            w[k + 14] = end > 0.0f ? end : 1e20f;
        }
        multi_mesh->set_mesh(mesh);
        multi_mesh->set_instance_count(data.size() / ChunkDetailGenerator::Stride);
        multi_mesh->set_buffer(data);
        multi_mesh_instance->set_material_override(detail->get_material());
        PopulationRendering::set_lod_range(multi_mesh_instance,
                                           PopulationRendering::effective_material(mesh, detail->get_material()), begin,
                                           end, margin);
        multi_mesh_instance->set_cast_shadows_setting(detail->get_shadows_enabled()
                                                          ? MeshInstance3D::SHADOW_CASTING_SETTING_ON
                                                          : MeshInstance3D::SHADOW_CASTING_SETTING_OFF);
    }
    _has_details = detail_count > 0;
    detail_state = DETAILS_SHOWN;
}

void JarVoxelChunk::clear_details()
{
    for (MultiMeshInstance3D *multi_mesh_instance : multi_mesh_instances)
        hide_multi_mesh_instance(multi_mesh_instance);
    _has_details = false;
    detail_state = DETAILS_NONE;
    detail_token++; // a job in flight for this chunk is dropped
}

void JarVoxelChunk::hide_multi_mesh_instance(MultiMeshInstance3D *instance)
{
    instance->set_visible(false);
    if (instance->get_multimesh().is_valid() && instance->get_multimesh()->get_instance_count() > 0)
        instance->get_multimesh()->set_instance_count(0);
}

// The terrain shader works in volume space (so textures and up directions
// follow a moving volume); its inputs are on the volume's material.
void JarVoxelChunk::apply_rendering(const JarVoxelVolume &terrain)
{
    apply_terrain_rendering(mesh_instance, terrain);
}

void JarVoxelChunk::apply_terrain_rendering(GeometryInstance3D *instance, const JarVoxelVolume &terrain)
{
    const Ref<Material> volumeMaterial = terrain.get_runtime_material();
    if (volumeMaterial.is_valid() && instance->get_material_override() != volumeMaterial)
        instance->set_material_override(volumeMaterial);
}

JarVoxelVolume *JarVoxelChunk::get_volume() const
{
    return Object::cast_to<JarVoxelVolume>(get_parent());
}

void JarVoxelChunk::update_collision_mesh()
{
    const VoxelStats::Scope stage(VoxelStats::MainColliders);
    // if(is_queued_for_deletion()) return;
    if (!_chunk_mesh_data)
        return;
    if (_chunk_mesh_data->collision_mesh.is_empty())
        _chunk_mesh_data->build_collision_mesh(); // LOD above MaxCollisionLod: not prebuilt
    set_collider_active(true);
    concave_polygon_shape->set_faces(_chunk_mesh_data->collision_mesh);
    VoxelStats::add_count(VoxelStats::CollidersBuilt);
    _chunk_mesh_data.reset(); // physics has its own copy now
}

void JarVoxelChunk::delete_chunk()
{
    // Implementation of UpdateDetailMeshes(0) not shown
}
