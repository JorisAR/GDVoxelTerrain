#ifndef VOXEL_TERRAIN_H
#define VOXEL_TERRAIN_H

#include "mesh_compute_scheduler.h"
#include "sdf_modification.h"
#include "signed_distance_field.h"
#include "terrain_detail.h"
#include "voxel_lod.h"
#include "voxel_octree_node.h"
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <atomic>
#include <queue>
#include <thread>
#include <vector>
#include "voxel_terrain_material.h"

namespace godot
{

  class JarWorld;

  class JarVoxelTerrain : public Node3D
  {
    GDCLASS(JarVoxelTerrain, Node3D);

  private:
    std::unique_ptr<MeshComputeScheduler> _meshComputeScheduler;

    std::vector<float> _voxelEpsilons;

    Ref<JarSignedDistanceField> _sdf;
    std::unique_ptr<VoxelOctreeNode> _voxelRoot;

    struct ChunkComparator
    {
      bool operator()(const VoxelOctreeNode *a, const VoxelOctreeNode *b) const
      {
        return a->get_lod() > b->get_lod();
      }
    };

    std::queue<ModifySettings> _modifySettingsQueue;
    std::queue<VoxelOctreeNode *> _updateChunkCollidersQueue;
    // std::queue<VoxelOctreeNode*> _deleteChunkQueue;

    // Exported variables
    float _octreeScale = 1.0f;
    int _size = 8;
    int _chunk_size_log2 = 4; // each chunk is 2^4 = 16*16*16 voxels
    int _chunkSize = 16;

    Ref<PackedScene> _chunkScene;

    std::atomic<bool> _isBuilding{false};
    std::thread _buildThread;
    bool _cubicVoxels = false;

    // MATERIALS
    VoxelMaterialMode _materialMode = VoxelMaterialMode::VOXEL_MATERIAL_MODE_DISCRETE_CHANNEL_SPLATTING;
    TypedArray<Material> _materials;

    // PERFORMANCE
    int _maxConcurrentTasks = 12;
    int _updatedCollidersPerSecond = 128;
    int _collider_max_lod_threshold = 1;

    // LOD
    JarVoxelLoD _voxelLod;
    int _lod_level_count = 20;
    int _lod_shell_size = 2;
    bool _lod_automatic_update = true;
    float _lod_automatic_update_distance = 64.0f;

    // POPULATION
    TypedArray<JarTerrainDetail> _terrainDetails;

    // TERRAIN DESTRUCTION
    bool _destruction_enable_separating_loose_chunks = true;
    int _destruction_max_rigidybody_volume;

    void build();
    void _notification(int what);
    void initialize();
    void process();
    void process_chunk_queue(float delta);
    void generate_epsilons();
    void process_modify_queue();

    JarWorld *_world = nullptr;
    Node3D *_playerNode = nullptr;

  protected:
    static void _bind_methods();

  public:
    JarVoxelTerrain();
    ~JarVoxelTerrain() override;

    void modify_using_sdf(const Ref<JarSdfModification> sdf);

    void spawn_debug_spheres_in_bounds(const Vector3 &position, const float range);

    void force_update_lod();

    // chunks
    void enqueue_chunk_collider(VoxelOctreeNode *node);
    void enqueue_chunk_update(VoxelOctreeNode &node);

    // properties
    bool is_building() const;
    // MeshComputeScheduler *get_mesh_scheduler() const;

    bool need_normals() const { return false; } // for dual contouring

    // properties
    Node3D *get_player_node() const;
    void set_player_node(Node3D *playerNode);

    JarWorld *get_world_node() const;
    void set_world_node(JarWorld *worldNode);

    Ref<JarSignedDistanceField> get_sdf() const;
    void set_sdf(const Ref<JarSignedDistanceField> &sdf);

    float get_octree_scale() const;
    void set_octree_scale(float value);

    int get_size() const;
    void set_size(int value);

    int get_chunk_size_log2() const;
    void set_chunk_size_log2(int value);

    int get_chunk_size() const;

    Ref<PackedScene> get_chunk_scene() const;
    void set_chunk_scene(const Ref<PackedScene> &value);

    bool get_cubic_voxels() const;
    void set_cubic_voxels(bool value);

    // MATERIALS

    VoxelMaterialMode get_material_mode() const;
    void set_material_mode(VoxelMaterialMode mode);

    TypedArray<Material> get_materials() const;
    void set_materials(const TypedArray<Material> &materials);

    // PEROFRMANCE
    int get_max_concurrent_tasks() const;
    void set_max_concurrent_tasks(int value);

    int get_updated_colliders_per_second() const;
    void set_updated_colliders_per_second(int value);

    int get_collider_max_lod_threshold() const;
    void set_collider_max_lod_threshold(int value);

    // LOD

    int get_lod_level_count() const;
    void set_lod_level_count(int value);

    int get_lod_shell_size() const;
    void set_lod_shell_size(int value);

    bool get_lod_automatic_update() const;
    void set_lod_automatic_update(bool value);

    float get_lod_automatic_update_distance() const;
    void set_lod_automatic_update_distance(float value);

    void get_voxel_leaves_in_bounds(const Bounds &bounds, std::vector<VoxelOctreeNode *> &nodes) const;
    void get_voxel_leaves_in_bounds(const Bounds &bounds, int lod, std::vector<VoxelOctreeNode *> &nodes) const;
    void get_voxel_leaves_in_bounds_excluding_bounds(const Bounds &bounds, const Bounds &excludeBounds, int lod,
                                                     std::vector<VoxelOctreeNode *> &nodes) const;

    // LOD
    glm::vec3 get_camera_position() const;
    int desired_lod(const VoxelOctreeNode &node);
    int lod_at(const glm::vec3 &position) const;

    // POPULATION
    void set_terrain_details(const TypedArray<JarTerrainDetail> &details);
    TypedArray<JarTerrainDetail> get_terrain_details() const;
  };
}

#endif // VOXEL_TERRAIN_H
