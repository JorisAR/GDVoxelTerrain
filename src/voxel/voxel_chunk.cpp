#include "voxel_chunk.h"
#include "chunk_detail_generator.h"
#include "voxel_volume.h"
#include "voxel_octree_node.h"
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

void JarVoxelChunk::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_mesh_instance"), &JarVoxelChunk::get_mesh_instance);
    ClassDB::bind_method(D_METHOD("set_mesh_instance", "mesh_instance"), &JarVoxelChunk::set_mesh_instance);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh_instance", PROPERTY_HINT_NODE_TYPE, "MeshInstance3D"),
                 "set_mesh_instance", "get_mesh_instance");

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

    ClassDB::bind_method(D_METHOD("get_collider_lod_threshold"), &JarVoxelChunk::get_collider_lod_threshold);
    ClassDB::bind_method(D_METHOD("set_collider_lod_threshold", "collider_lod_threshold"),
                         &JarVoxelChunk::set_collider_lod_threshold);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "collider_lod_threshold"), "set_collider_lod_threshold",
                 "get_collider_lod_threshold");
}

void JarVoxelChunk::_update_multi_mesh_instances(int n)
{
    int count = multi_mesh_instances.size();

    if(count == n) return;
    if(count < n) {
        for (int i = count; i < n; i++)
        {
            MultiMeshInstance3D* multi_mesh_instance = memnew(MultiMeshInstance3D);
            add_child(multi_mesh_instance);
            multi_mesh_instances.push_back(multi_mesh_instance);
            multi_mesh_instance->set_position(Vector3(0, 0, 0));
        }
    }
    else
    {
        for (int i = n; i < count; i++)
        {
            remove_child(multi_mesh_instances[i]);
            multi_mesh_instances[i]->queue_free();
            multi_mesh_instances[i] = nullptr;
        }
        //remove null instances
        multi_mesh_instances.erase( std::remove( std::begin(multi_mesh_instances), std::end(multi_mesh_instances), nullptr ), std::end(multi_mesh_instances) ) ;
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

uint16_t JarVoxelChunk::get_boundaries() const
{
    return boundaries;
}

void JarVoxelChunk::set_boundaries(uint16_t p_h2lboundaries)
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


void JarVoxelChunk::update_chunk(JarVoxelVolume &terrain, VoxelOctreeNode *node, ChunkMeshData *chunk_mesh_data)
{
    _chunk_mesh_data.reset(chunk_mesh_data);
    array_mesh = Ref<ArrayMesh>(Object::cast_to<ArrayMesh>(*mesh_instance->get_mesh()));
    concave_polygon_shape =
        Ref<ConcavePolygonShape3D>(Object::cast_to<ConcavePolygonShape3D>(*collision_shape->get_shape()));
    material = Ref<ShaderMaterial>(Object::cast_to<ShaderMaterial>(*mesh_instance->get_material_override()));
    lod = chunk_mesh_data->lod;
    boundaries = chunk_mesh_data->boundaries;
    mesh_version = chunk_mesh_data->mesh_version;
    edge_chunk = chunk_mesh_data->edge_chunk;
    auto old_bounds = bounds;
    bounds = chunk_mesh_data->bounds;
    auto position = bounds.get_center();
    // auto position = bounds.get_center() * 1.05f;
    set_position({position.x, position.y, position.z});

    array_mesh->clear_surfaces();
    // CUSTOM0 holds material weights 4..7 as 4 bytes per vertex.
    array_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, chunk_mesh_data->mesh_array, Array(), Dictionary(),
                                        Mesh::ARRAY_CUSTOM_RGBA8_UNORM << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT);
    apply_rendering(terrain);

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
        collision_shape->set_disabled(true);
    }

    //generate details. Chunks are pooled and change LOD, so anything that should
    //not show details must drop the instances left over from its previous use.
    // Transforms were computed on the mesh thread; only the upload happens here.
    const std::vector<PackedFloat32Array> &buffers = chunk_mesh_data->detail_buffers;
    TypedArray<JarTerrainDetail> terrain_details = terrain.get_terrain_details();
    const int detail_count = lod > 0 ? 0 : std::min<int>(buffers.size(), terrain_details.size());
    _update_multi_mesh_instances(detail_count);
    for (int i = 0; i < detail_count; i++)
    {
        MultiMeshInstance3D *multi_mesh_instance = multi_mesh_instances[i];
        Ref<JarTerrainDetail> detail = terrain_details[i];
        Ref<MultiMesh> multi_mesh = multi_mesh_instance->get_multimesh();
        if (multi_mesh.is_null())
        {
            multi_mesh.instantiate();
            multi_mesh->set_transform_format(MultiMesh::TRANSFORM_3D);
            multi_mesh_instance->set_multimesh(multi_mesh);
        }
        multi_mesh->set_mesh(detail->get_mesh());
        multi_mesh->set_instance_count(buffers[i].size() / 12);
        if (!buffers[i].is_empty())
            multi_mesh->set_buffer(buffers[i]);
        multi_mesh_instance->set_material_override(detail->get_material());
        multi_mesh_instance->set_visibility_range_end(detail->get_draw_distance());
        multi_mesh_instance->set_cast_shadows_setting(detail->get_shadows_enabled()
                                                          ? MeshInstance3D::SHADOW_CASTING_SETTING_ON
                                                          : MeshInstance3D::SHADOW_CASTING_SETTING_OFF);
    }

    // The CPU copy of the mesh is only needed for a collider that is still to
    // be built (update_collision_mesh releases it); the renderer keeps its own.
    if (!generate_collider)
        _chunk_mesh_data.reset();

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

// Material and per-chunk shader inputs. The terrain shader works in volume
// space (so textures and up directions follow a moving volume): it needs this
// chunk's origin in the volume and the volume's up field.
void JarVoxelChunk::apply_rendering(const JarVoxelVolume &terrain)
{
    const Ref<Material> volumeMaterial = terrain.get_runtime_material();
    if (volumeMaterial.is_valid() && mesh_instance->get_material_override() != volumeMaterial)
        mesh_instance->set_material_override(volumeMaterial);
    const UpField &up = terrain.get_up_field();
    const glm::vec3 origin = bounds.get_center();
    mesh_instance->set_instance_shader_parameter("jar_chunk_origin", Vector3(origin.x, origin.y, origin.z));
    mesh_instance->set_instance_shader_parameter("jar_up_center",
                                                 Vector4(up.center.x, up.center.y, up.center.z, up.sea_level));
    mesh_instance->set_instance_shader_parameter("jar_up_axis",
                                                 Vector4(up.axis.x, up.axis.y, up.axis.z, float(up.mode)));
}

JarVoxelVolume *JarVoxelChunk::get_volume() const
{
    return Object::cast_to<JarVoxelVolume>(get_parent());
}

void JarVoxelChunk::update_collision_mesh()
{
    // if(is_queued_for_deletion()) return;
    if (!_chunk_mesh_data)
        return;
    if (_chunk_mesh_data->collision_mesh.is_empty())
        _chunk_mesh_data->build_collision_mesh(); // LOD above MaxCollisionLod: not prebuilt
    collision_shape->set_disabled(false);
    concave_polygon_shape->set_faces(_chunk_mesh_data->collision_mesh);
    _chunk_mesh_data.reset(); // physics has its own copy now
}

void JarVoxelChunk::delete_chunk()
{
    // Implementation of UpdateDetailMeshes(0) not shown
}
