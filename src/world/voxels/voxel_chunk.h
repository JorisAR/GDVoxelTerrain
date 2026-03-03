#ifndef JAR_VOXEL_CHUNK_H
#define JAR_VOXEL_CHUNK_H

#include "bounds.h"
#include "extracted_mesh_data.h"
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <vector>

namespace godot
{

  class JarVoxelTerrain;
  class VoxelOctreeNode;

  class JarVoxelChunk : public StaticBody3D
  {
    GDCLASS(JarVoxelChunk, StaticBody3D);

  private:
    int lod = 0;
    uint16_t boundaries = 0;
    bool edge_chunk = false;
    Bounds bounds;

    // node references
    JarVoxelTerrain *_terrain = nullptr;
    MeshInstance3D *mesh_instance = nullptr;
    CollisionShape3D *collision_shape = nullptr;
    PackedVector3Array _collision_mesh_faces;

    // data containers
    Ref<ArrayMesh> array_mesh;
    Ref<ConcavePolygonShape3D> concave_polygon_shape;
    Ref<ShaderMaterial> material;

    std::vector<MultiMeshInstance3D *> multi_mesh_instances;

    // std::vector<Ref<MultiMeshInstance3D>> multi_mesh_instance;
    //  Add references for grass and biome textures if necessary
    // ChunkMeshData* chunk_mesh_data;
    //  bool update_when_ready;

    static void _bind_methods();

    void _update_multi_mesh_instances(int n);

  public:
    JarVoxelChunk();
    ~JarVoxelChunk();

    int get_lod() const;
    void set_lod(int p_lod);

    uint16_t get_boundaries() const;
    void set_boundaries(uint16_t p_boundaries);

    bool is_edge_chunk() const;
    void set_edge_chunk(bool p_edge_chunk);

    MeshInstance3D *get_mesh_instance() const;
    void set_mesh_instance(MeshInstance3D *p_mesh_instance);

    CollisionShape3D *get_collision_shape() const;
    void set_collision_shape(CollisionShape3D *p_collision_shape);

    Ref<ArrayMesh> get_array_mesh() const;
    void set_array_mesh(Ref<ArrayMesh> p_array_mesh);

    Ref<ConcavePolygonShape3D> get_concave_polygon_shape() const;
    void set_concave_polygon_shape(Ref<ConcavePolygonShape3D> p_concave_polygon_shape);

    Ref<ShaderMaterial> get_material() const;
    void set_material(Ref<ShaderMaterial> p_material);

    JarVoxelTerrain *get_terrain() const { return _terrain; }

    void update_chunk(JarVoxelTerrain &terrain, VoxelOctreeNode *node, ExtractedMeshData *chunk_mesh_data);
    void update_collision_mesh();
    void delete_chunk();
  };
}

#endif // JAR_VOXEL_CHUNK_H
