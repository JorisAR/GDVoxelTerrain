#ifndef VOXEL_TERRAIN_H
#define VOXEL_TERRAIN_H

#include "mesh_compute_scheduler.h"
#include "modify_settings.h"
#include "signed_distance_field.h"
#include "terrain_detail.h"
#include "voxel_lod.h"
#include "world.h"
#include "voxel_octree_node.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
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
#include <queue>
#include <vector>

using namespace godot;

class JarVoxelChunk;

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
    int _minChunkSize = 4; // each chunk is 2^4 = 16*16*16 voxels

    Ref<PackedScene> _chunkScene;

    std::atomic<bool> _isBuilding{false};
    int _chunkSize = 0;
    bool _cubicVoxels = false;

    // One persistent, managed build worker (replaces per-build detached threads,
    // which could not be joined and were still walking the octree at shutdown
    // -> SIGSEGV on exit). Cleanly stopped + joined in the destructor.
    std::thread _buildThread;
    std::mutex _buildMutex;
    std::condition_variable _buildCv;
    bool _buildRequested = false; // guarded by _buildMutex
    bool _buildShutdown = false;  // guarded by _buildMutex
    void build_worker();
    void stop_build_worker();

    // Chunk object pool: reuse chunk nodes instead of instantiate()/free() for the
    // thousands of chunks that stream in/out. Chunks are released on the build
    // worker thread (thread-safe queue) and reclaimed/reused on the main thread.
    std::vector<JarVoxelChunk *> _freeChunks;     // main-thread pool of reusable chunks
    std::vector<JarVoxelChunk *> _releasedChunks; // worker pushes here; main drains
    std::mutex _poolMutex;                        // guards _releasedChunks
    void process_pool();                          // main thread: released -> deactivate -> free pool

    // PERFORMANCE
    int _maxConcurrentTasks = 12;
    int _updatedCollidersPerSecond = 128;
    // Only generate physics colliders for chunks within this distance of the
    // camera (0 = unlimited = original behaviour). Colliders are already LOD-
    // limited; this further culls the ones the player can never touch, which is
    // the expensive part. Set it a bit larger than lod_automatic_update_distance.
    float _colliderDistance = 0.0f;

    // LOD
    JarVoxelLoD _voxelLod;
    int lod_level_count = 20;
    int lod_shell_size = 2;
    bool lod_automatic_update = true;
    float lod_automatic_update_distance = 64.0f;

    // POPULATION
    TypedArray<JarTerrainDetail> _terrainDetails;

    void build();
    void _notification(int what);
    void initialize();
    void process();
    void process_chunk_queue(float delta);
    void generate_epsilons();
    void process_modify_queue();

    // void process_delete_chunk_queue();

    Node3D *_playerNode = nullptr;
    JarWorld *_worldNode = nullptr;

  protected:
    static void _bind_methods();

  public:
    JarVoxelTerrain();
    ~JarVoxelTerrain();

    // Chunk pool API (used by VoxelOctreeNode). acquire() runs on the main thread;
    // release() is thread-safe (called from the build worker).
    JarVoxelChunk *pool_acquire();
    void pool_release(JarVoxelChunk *chunk);

    void modify(const Ref<JarSignedDistanceField> sdf, const SDF::Operation operation, const Vector3 &position,
                const float radius, const int material = 0);
    void sphere_edit(const Vector3 &position, const float radius, bool operation_union, const int material = 0);

    void spawn_debug_spheres_in_bounds(const Vector3 &position, const float range);

    void force_update_lod();

    // chunks
    void enqueue_chunk_collider(VoxelOctreeNode *node);
    void enqueue_chunk_update(VoxelOctreeNode &node);

    // properties
    bool is_building() const;
    // MeshComputeScheduler *get_mesh_scheduler() const;

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

    int get_min_chunk_size() const;
    void set_min_chunk_size(int value);

    int get_chunk_size() const;

    Ref<PackedScene> get_chunk_scene() const;
    void set_chunk_scene(const Ref<PackedScene> &value);

    bool get_cubic_voxels() const;
    void set_cubic_voxels(bool value);

    // PEROFRMANCE
    int get_max_concurrent_tasks() const;
    void set_max_concurrent_tasks(int value);

    int get_updated_colliders_per_second() const;
    void set_updated_colliders_per_second(int value);

    float get_collider_distance() const;
    void set_collider_distance(float value);
    // True if a chunk at `pos` should get a collider under the distance cull.
    bool is_in_collider_range(const glm::vec3 &pos) const;

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

VARIANT_ENUM_CAST(SDF::Operation);

#endif // VOXEL_TERRAIN_H
