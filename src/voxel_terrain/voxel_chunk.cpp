#include "voxel_chunk.h"
#include "chunk_detail_generator.h"
#include "voxel_terrain/voxel_terrain.h"
#include "voxel_terrain/voxel_octree_node.h"
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

uint8_t JarVoxelChunk::get_boundaries() const
{
    return boundaries;
}

void JarVoxelChunk::set_boundaries(uint8_t p_h2lboundaries)
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


void JarVoxelChunk::update_chunk(JarVoxelTerrain &terrain, VoxelOctreeNode *node, ChunkMeshData *chunk_mesh_data)
{
    _chunk_mesh_data = chunk_mesh_data;
    array_mesh = Ref<ArrayMesh>(Object::cast_to<ArrayMesh>(*mesh_instance->get_mesh()));
    concave_polygon_shape =
        Ref<ConcavePolygonShape3D>(Object::cast_to<ConcavePolygonShape3D>(*collision_shape->get_shape()));
    material = Ref<ShaderMaterial>(Object::cast_to<ShaderMaterial>(*mesh_instance->get_material_override()));
    lod = chunk_mesh_data->lod;
    boundaries = chunk_mesh_data->boundaries;
    edge_chunk = chunk_mesh_data->edge_chunk;
    auto old_bounds = bounds;
    bounds = chunk_mesh_data->bounds;
    auto position = bounds.get_center();
    // auto position = bounds.get_center() * 1.05f;
    set_position({position.x, position.y, position.z});

    array_mesh->clear_surfaces();
    array_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, chunk_mesh_data->mesh_array);

    bool generate_collider = lod <= collider_lod_threshold && terrain.is_in_collider_range(position);

    if (generate_collider)
    {
        // collision_shape->set_disabled(!chunk_mesh_data->has_collision_mesh());
        // concave_polygon_shape->set_faces(chunk_mesh_data.create_collision_mesh());
        terrain.enqueue_chunk_collider(node);
    }
    else
    {
        collision_shape->set_disabled(true);
    }

    //generate details
    if(lod <= 0) {
        ChunkDetailGenerator generator = ChunkDetailGenerator(terrain.get_world_node());
        TypedArray<JarTerrainDetail> terrain_details = terrain.get_terrain_details();
        TypedArray<MultiMesh> multi_meshes = generator.generate_details(terrain_details, *chunk_mesh_data);

        _update_multi_mesh_instances(multi_meshes.size());
        for (int i = 0; i < multi_meshes.size(); i++)
        {
            MultiMeshInstance3D* multi_mesh_instance = multi_mesh_instances[i];
            Ref<JarTerrainDetail> detail = terrain_details[i];
            multi_mesh_instance->set_multimesh(multi_meshes[i]);
            multi_mesh_instance->set_material_override(detail->get_material());
            multi_mesh_instance->set_cast_shadows_setting(detail->get_shadows_enabled() ? MeshInstance3D::SHADOW_CASTING_SETTING_ON : MeshInstance3D::SHADOW_CASTING_SETTING_OFF);
        }
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

void JarVoxelChunk::update_collision_mesh()
{
    // if(is_queued_for_deletion()) return;
    collision_shape->set_disabled(false);
    concave_polygon_shape->set_faces(_chunk_mesh_data->create_collision_mesh());
}

void JarVoxelChunk::delete_chunk()
{
    // Implementation of UpdateDetailMeshes(0) not shown
}
