#ifndef JAR_VOXEL_CHUNK_H
#define JAR_VOXEL_CHUNK_H

#include "bounds.h"
#include "chunk_mesh_data.h"
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/concave_polygon_shape3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <memory>
#include <vector>

using namespace godot;

class JarVoxelVolume;
class VoxelOctreeNode;

class JarVoxelChunk : public Node3D
{
    GDCLASS(JarVoxelChunk, Node3D);

  private:
    int lod = 0;
    int collider_lod_threshold = 1;
    uint16_t boundaries = 0;
    bool edge_chunk = false;
    Bounds bounds;

    // node references
    std::unique_ptr<ChunkMeshData> _chunk_mesh_data; // owned; kept for (deferred) collider builds
    MeshInstance3D* mesh_instance = nullptr;
    CollisionShape3D* collision_shape = nullptr;
    StaticBody3D* static_body = nullptr;

    // data containers
    Ref<ArrayMesh> array_mesh;
    Ref<ConcavePolygonShape3D> concave_polygon_shape;
    Ref<ShaderMaterial> material;

    std::vector<MultiMeshInstance3D*> multi_mesh_instances;

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

    int get_collider_lod_threshold() const;
    void set_collider_lod_threshold(int p_collider_lod_threshold);

    // 16 bits: high-to-low LOD seams in the low byte, low-to-high in the high byte.
    uint16_t get_boundaries() const;
    void set_boundaries(uint16_t p_boundaries);
    int mesh_version = -1; // JarVoxelVolume::get_mesh_version() this chunk was meshed with

    bool is_edge_chunk() const;
    void set_edge_chunk(bool p_edge_chunk);

    MeshInstance3D* get_mesh_instance() const;
    void set_mesh_instance(MeshInstance3D* p_mesh_instance);

    CollisionShape3D* get_collision_shape() const;
    void set_collision_shape(CollisionShape3D* p_collision_shape);

    StaticBody3D* get_static_body() const;
    void set_static_body(StaticBody3D* p_static_body);

    Ref<ArrayMesh> get_array_mesh() const;
    void set_array_mesh(Ref<ArrayMesh> p_array_mesh);

    Ref<ConcavePolygonShape3D> get_concave_polygon_shape() const;
    void set_concave_polygon_shape(Ref<ConcavePolygonShape3D> p_concave_polygon_shape);

    Ref<ShaderMaterial> get_material() const;
    void set_material(Ref<ShaderMaterial> p_material);

    // Takes ownership of chunk_mesh_data.
    void update_chunk(JarVoxelVolume &terrain, VoxelOctreeNode *node, ChunkMeshData *chunk_mesh_data);
    void update_collision_mesh();
    void apply_rendering(const JarVoxelVolume &terrain);
    // The volume this chunk belongs to, e.g. from a physics raycast hit:
    // collider -> chunk (its body's owner) -> volume.
    JarVoxelVolume *get_volume() const;
    bool collider_queued = false; // owned by JarVoxelVolume's collider queue
    void delete_chunk();

    // Object-pool support: instead of freeing a chunk, park it (hidden + collision
    // off, still attached to the terrain) so it can be reused. Avoids the
    // instantiate()/free() churn across thousands of chunks. Both run on main thread.
    void deactivate()
    {
        set_visible(false);
        if (collision_shape)
            collision_shape->set_disabled(true);
    }
    void reactivate()
    {
        // Collision stays disabled: the shape still holds the previous owner's
        // faces. update_collision_mesh() re-enables it once rebuilt.
        set_visible(true);
    }
};

#endif // JAR_VOXEL_CHUNK_H
