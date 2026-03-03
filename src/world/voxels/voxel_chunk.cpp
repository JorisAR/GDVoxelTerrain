#include "voxel_chunk.h"
#include "chunk_detail_generator.h"
#include "voxel_octree_node.h"
#include "voxel_terrain.h"
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>

using namespace godot;

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

    ClassDB::bind_method(D_METHOD("get_terrain"), &JarVoxelChunk::get_terrain);
}

void JarVoxelChunk::_update_multi_mesh_instances(int n)
{
    int count = multi_mesh_instances.size();

    if (count == n)
        return;
    if (count < n)
    {
        for (int i = count; i < n; i++)
        {
            MultiMeshInstance3D *multi_mesh_instance = memnew(MultiMeshInstance3D);
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
        // remove null instances
        multi_mesh_instances.erase(
            std::remove(std::begin(multi_mesh_instances), std::end(multi_mesh_instances), nullptr),
            std::end(multi_mesh_instances));
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

void JarVoxelChunk::update_chunk(JarVoxelTerrain &terrain, VoxelOctreeNode *node, ExtractedMeshData *chunk_mesh_data)
{
    _terrain = &terrain;
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
    bool use_materials = true;
    for (int i = 0; i < chunk_mesh_data->mesh_arrays.size(); i++)
    {
        array_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, chunk_mesh_data->mesh_arrays[i]);

        if (i < chunk_mesh_data->material_indices.size())
        {
            auto material_index = chunk_mesh_data->material_indices[i];
            if (material_index < terrain.get_materials().size())
            {
                mesh_instance->set_surface_override_material(i, terrain.get_materials()[material_index]);
            }
            else
                use_materials = false;
        }
        else
            use_materials = false;
    }
    if (use_materials) // if so, clear override
        mesh_instance->set_material_override(nullptr);

    bool generate_collider = lod <= terrain.get_collider_max_lod_threshold();

    if (generate_collider)
    {
        _collision_mesh_faces = chunk_mesh_data->create_collision_mesh();
        if (_collision_mesh_faces.is_empty())
        {
            collision_shape->set_disabled(true);
        }
        else
        {
            terrain.enqueue_chunk_collider(node);
        }
    }
    else
    {
        collision_shape->set_disabled(true);
        _collision_mesh_faces = PackedVector3Array();
    }

    // generate details
    if (lod <= 0)
    {
        ChunkDetailGenerator generator = ChunkDetailGenerator(terrain.get_world_node(), terrain.get_material_mode());
        TypedArray<JarTerrainDetail> terrain_details = terrain.get_terrain_details();
        TypedArray<MultiMesh> multi_meshes = generator.generate_details(terrain_details, *chunk_mesh_data);

        _update_multi_mesh_instances(multi_meshes.size());
        for (int i = 0; i < multi_meshes.size(); i++)
        {
            MultiMeshInstance3D *multi_mesh_instance = multi_mesh_instances[i];
            Ref<JarTerrainDetail> detail = terrain_details[i];
            multi_mesh_instance->set_multimesh(multi_meshes[i]);
            multi_mesh_instance->set_material_override(detail->get_material());
            multi_mesh_instance->set_cast_shadows_setting(detail->get_shadows_enabled()
                                                              ? MeshInstance3D::SHADOW_CASTING_SETTING_ON
                                                              : MeshInstance3D::SHADOW_CASTING_SETTING_OFF);
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
    if (_collision_mesh_faces.is_empty())
    {
        collision_shape->set_disabled(true);
        return;
    }
    collision_shape->set_disabled(false);
    concave_polygon_shape->set_faces(_collision_mesh_faces);
}

void JarVoxelChunk::delete_chunk()
{
    // Implementation of UpdateDetailMeshes(0) not shown
}
