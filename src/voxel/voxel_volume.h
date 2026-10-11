#ifndef VOXEL_VOLUME_H
#define VOXEL_VOLUME_H

#include "mesh_compute_scheduler.h"
#include "modify_settings.h"
#include "signed_distance_field.h"
#include "terrain_detail.h"
#include "up_field.h"
#include "voxel_lod.h"
#include "voxel_octree_node.h"
#include <atomic>
#include <condition_variable>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/random_number_generator.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

using namespace godot;

class JarVoxelChunk;

class JarVoxelVolume : public Node3D
{
    GDCLASS(JarVoxelVolume, Node3D);

  private:
    std::unique_ptr<MeshComputeScheduler> _meshComputeScheduler;


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
    std::vector<JarVoxelChunk *> _pendingColliders; // main thread; see process_chunk_queue()
    // std::queue<VoxelOctreeNode*> _deleteChunkQueue;

    // Exported variables
    float _voxelSize = 1.0f;  // meters per voxel at full detail (LOD 0)
    float _extent = 16384.0f; // edge length of the cube the volume covers, centered on its origin
    int _depth = 14;          // octree levels, derived from extent / voxel size in initialize()

    Ref<PackedScene> _chunkScene;

    std::atomic<bool> _isBuilding{false};
    bool _initialBuildRequested = false; // first build waits for process(), when the player is in the tree
    bool _cubicVoxels = false;
    std::atomic<int> _meshingAlgorithm{0}; // MeshingAlgorithm; read by mesh threads
    std::atomic<float> _dcFeatureThreshold{0.1f};
    // Bumped when meshing settings change; chunks meshed with an older version
    // are remeshed by the next build.
    std::atomic<int> _meshVersion{0};
    bool _rebuildRequested = false; // main thread: rebuild as soon as idle

    // One persistent, managed build worker (replaces per-build detached threads,
    // which could not be joined and were still walking the octree at shutdown
    // -> SIGSEGV on exit). Cleanly stopped + joined in the destructor.
    std::thread _buildThread;
    std::mutex _buildMutex;
    std::condition_variable _buildCv;
    bool _buildRequested = false;             // guarded by _buildMutex
    std::vector<ModifySettings> _workerEdits; // guarded by _buildMutex; applied before any build
    bool _buildShutdown = false;              // guarded by _buildMutex
    void build_worker();
    void build_octree(); // build worker only
    void stop_build_worker();

    // Chunk object pool: reuse chunk nodes instead of instantiate()/free() for the
    // thousands of chunks that stream in/out. Chunks are released on the build
    // worker thread (thread-safe queue) and reclaimed/reused on the main thread.
    std::vector<JarVoxelChunk *> _freeChunks;     // main-thread pool of reusable chunks
    std::vector<JarVoxelChunk *> _releasedChunks; // worker pushes here; main drains
    std::mutex _poolMutex;                        // guards _releasedChunks
    void process_pool();                          // main thread: released -> deactivate -> free pool

    // PERFORMANCE
    int _updatedCollidersPerSecond = 0; // 0 = uncapped (time budget only)
    // Only generate physics colliders for chunks within this distance of the
    // camera (0 = unlimited = original behaviour). Colliders are already LOD-
    // limited; this further culls the ones the player can never touch, which is
    // the expensive part. Set it a bit larger than lod_automatic_update_distance.
    float _colliderDistance = 0.0f;
    float _colliderBudgetMs = 2.0f;

    // LOD
    JarVoxelLoD _voxelLod;
    int _lod_shell_size = 2;
    float _lodDetailDistance = 0.0f;
    bool _lod_automatic_update = true;
    float _lod_automatic_update_distance = 64.0f;

    // POPULATION
    TypedArray<JarTerrainDetail> _terrainDetails;
    UpField _upField;

    // TERRAIN DESTRUCTION
    // bool _separate_loose_chunks = true;
    // int _max_rigidybody_volume;

    void build();
    void _notification(int what);
    void initialize();
    void process();
    void process_chunk_queue(float delta);
    void process_modify_queue();
    // Resolves an edit into volume space and queues it. `limit` (volume-local)
    // further restricts the affected region, e.g. modify()'s radius.
    void queue_edit(const Ref<JarSignedDistanceField> &sdf, SDF::Operation operation, const Transform3D &transform,
                    int material, float smoothness, const Bounds &limit = Bounds::infinite());

    // void process_delete_chunk_queue();

  protected:
    static void _bind_methods();

  public:
    JarVoxelVolume();
    ~JarVoxelVolume();

    // Chunk pool API (used by VoxelOctreeNode). acquire() runs on the main thread;
    // release() is thread-safe (called from the build worker).
    JarVoxelChunk *pool_acquire();
    void pool_release(JarVoxelChunk *chunk);

    // Script-facing operations (GDScript sees them as JarVoxelVolume.Operation).
    enum Operation
    {
        SDF_OPERATION_UNION = SDF::SDF_OPERATION_UNION,
        SDF_OPERATION_SUBTRACTION = SDF::SDF_OPERATION_SUBTRACTION,
        SDF_OPERATION_INTERSECTION = SDF::SDF_OPERATION_INTERSECTION,
        SDF_OPERATION_SMOOTH_UNION = SDF::SDF_OPERATION_SMOOTH_UNION,
        SDF_OPERATION_SMOOTH_SUBTRACTION = SDF::SDF_OPERATION_SMOOTH_SUBTRACTION,
        SDF_OPERATION_SMOOTH_INTERSECTION = SDF::SDF_OPERATION_SMOOTH_INTERSECTION,
    };

    // Places `sdf` with a global transform and combines it with the volume.
    void edit(const Ref<JarSignedDistanceField> &sdf, const Operation operation, const Transform3D &transform,
              const int material = 0, const float smoothness = 0.0f);
    void modify(const Ref<JarSignedDistanceField> sdf, const Operation operation, const Vector3 &position,
                const float radius, const int material = 0);
    void sphere_edit(const Vector3 &position, const float radius, bool operation_union, const int material = 0);

    void spawn_debug_spheres_in_bounds(const Vector3 &position, const float range);

    void force_update_lod();

    // chunks
    void enqueue_chunk_collider(JarVoxelChunk *chunk);
    void enqueue_chunk_update(VoxelOctreeNode &node);

    // properties
    bool is_building() const;
    bool is_meshing() const;
    // MeshComputeScheduler *get_mesh_scheduler() const;

    enum UpMode
    {
        UP_MODE_AXIS = UpField::AXIS,
        UP_MODE_RADIAL = UpField::RADIAL,
    };

    // Up direction / height (population rules, player controllers). Copy is
    // taken by worker threads; only change it while the volume is idle.
    const UpField &get_up_field() const
    {
        return _upField;
    }
    UpMode get_up_mode() const;
    void set_up_mode(UpMode mode);
    Vector3 get_up_axis() const;
    void set_up_axis(const Vector3 &axis);
    Vector3 get_up_center() const;
    void set_up_center(const Vector3 &center);
    float get_sea_level() const;
    void set_sea_level(float value);
    // Script helpers taking and returning global-space values.
    Vector3 get_up_direction_at(const Vector3 &global_position) const;
    // Ray marches the volume's base SDF (edits are not included) from `from`
    // to `to`, in global space. Works at any distance, without colliders.
    // Returns {position, normal} or an empty dictionary.
    Dictionary raycast_sdf(const Vector3 &from, const Vector3 &to) const;
    float get_height_at(const Vector3 &global_position) const;

    Ref<JarSignedDistanceField> get_sdf() const;
    // Hot-path access without Ref refcounting (an atomic shared by all build threads).
    JarSignedDistanceField *get_sdf_ptr() const
    {
        return _sdf.ptr();
    }
    void set_sdf(const Ref<JarSignedDistanceField> &sdf);

    // Chunks are 2^ChunkSizeLog2 voxels along each axis at every LOD (the
    // mesher's lookup tables are built for 16).
    static constexpr int ChunkSizeLog2 = 4;

    float get_voxel_size() const;
    void set_voxel_size(float value);
    float get_extent() const;
    void set_extent(float value);
    int get_depth() const { return _depth; }

    int get_min_chunk_size() const { return ChunkSizeLog2; }
    int get_chunk_size() const { return 1 << ChunkSizeLog2; }

    Ref<PackedScene> get_chunk_scene() const;
    void set_chunk_scene(const Ref<PackedScene> &value);

    bool get_cubic_voxels() const;
    void set_cubic_voxels(bool value);

    enum MeshingAlgorithm
    {
        MESHING_SURFACE_NETS = 0,
        MESHING_DUAL_CONTOURING = 1,
    };
    MeshingAlgorithm get_meshing_algorithm() const { return static_cast<MeshingAlgorithm>(_meshingAlgorithm.load()); }
    void set_meshing_algorithm(MeshingAlgorithm value);
    float get_dual_contouring_feature_threshold() const { return _dcFeatureThreshold; }
    void set_dual_contouring_feature_threshold(float value);
    int get_mesh_version() const { return _meshVersion; }
    // Remeshes every chunk (e.g. after changing meshing settings at runtime).
    void remesh_all();

    // PERFORMANCE
    int get_updated_colliders_per_second() const;
    void set_updated_colliders_per_second(int value);

    float get_collider_distance() const;
    float get_collider_budget_ms() const;
    void set_collider_budget_ms(float value);
    void set_collider_distance(float value);
    // True if a chunk at `pos` should get a collider under the distance cull.
    bool is_in_collider_range(const glm::vec3 &pos) const;

    // LOD

    float get_lod_detail_distance() const;
    void set_lod_detail_distance(float value);

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
    float distance_to_nearest_viewer(const glm::vec3 &position) const;
    int desired_lod(const VoxelOctreeNode &node);
    int lod_at(const glm::vec3 &position) const;

    // POPULATION
    void set_terrain_details(const TypedArray<JarTerrainDetail> &details);
    TypedArray<JarTerrainDetail> get_terrain_details() const;
};

VARIANT_ENUM_CAST(JarVoxelVolume::Operation);
VARIANT_ENUM_CAST(JarVoxelVolume::UpMode);
VARIANT_ENUM_CAST(JarVoxelVolume::MeshingAlgorithm);

#endif // VOXEL_VOLUME_H
