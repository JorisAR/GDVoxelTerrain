#ifndef JAR_VOXEL_DEBRIS_H
#define JAR_VOXEL_DEBRIS_H

#include "island_finder.h"
#include "volume_extension.h"
#include "voxel_block.h"
#include "voxel_block_sdf.h"
#include <atomic>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/physics_direct_body_state3d.hpp>
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

using namespace godot;

class JarVoxelVolume;

// A piece of terrain that broke off a volume (see JarVoxelDebris). Its shape
// is the voxel block it was cut out as, so it can be put back (merged) into a
// volume wherever it comes to rest.
class JarVoxelDebrisBody : public RigidBody3D
{
    GDCLASS(JarVoxelDebrisBody, RigidBody3D);
    friend class JarVoxelDebris;

  private:
    std::shared_ptr<const VoxelBlock> _block; // in its volume's space when it broke off
    glm::vec3 _centroid{0.0f};                // the block point at this body's origin
    int _cells = 0;
    double _restTime = 0.0; // seconds spent (nearly) still
    bool _merging = false;
    // Shaded as the terrain it was: its own copy of the volume's material,
    // whose volume space stays fixed to the piece (see pin_textures).
    Ref<ShaderMaterial> _material;
    MeshInstance3D *_mesh = nullptr;
    glm::vec3 _meshOrigin{0.0f}; // the mesh's origin in its volume's space when it broke off
    void pin_textures();

  public:
    // Solid voxels in the piece.
    int get_voxel_count() const { return _cells; }
    bool is_merging() const { return _merging; }
    void _integrate_forces(PhysicsDirectBodyState3D *state) override;

  protected:
    static void _bind_methods();
};

// Makes terrain that edits cut loose fall: add it as a child of a
// JarVoxelVolume. After every edit, solid pieces no longer connected to the
// rest of the terrain (and at most max_size voxels across) are cut out of the
// volume and become rigid bodies (JarVoxelDebrisBody) that fall with the
// volume's gravity, collide with it, the player and each other. A piece at
// rest on the volume is merged back into it (unless merge is off), where it
// lies. A volume extension: without this node nothing of it runs.
class JarVoxelDebris : public Node, public VolumeExtension
{
    GDCLASS(JarVoxelDebris, Node);

  private:
    bool _enabled = true;
    int _maxSize = 24;        // voxels across: larger pieces stay where they are
    bool _merge = true;       // merge pieces at rest back into the volume
    float _mergeDelay = 0.5f; // seconds a piece must rest before merging
    float _density = 2000.0f; // kg per cubic meter
    int _maxBodies = 16;      // the oldest is merged (or removed) beyond this
    uint32_t _collisionLayer = 0; // of the pieces; 0: the volume's
    uint32_t _collisionMask = 0;  // 0: the volume's layer and mask

    // Pieces smaller than this (in voxels) are removed instead of falling:
    // crumbs too small to be worth a body.
    static constexpr int MinCells = 4;
    // Regions larger than this (cells) are not searched (huge edits).
    static constexpr int64_t MaxRegionCells = 4 * 1024 * 1024;

    // A piece on its way out of the volume: being cut out (the edit queued
    // and meshed) until it is shown as a body.
    struct Piece
    {
        IslandFinder::Island island;
        Ref<JarVoxelBlockSdf> sdf;
        std::atomic<bool> removed{false}; // the cut was applied (build worker)
        bool committed = false;           // ...and is on screen
        std::atomic<bool> meshed{false};
        Array meshData; // written by the mesh job before `meshed`
    };
    // Shared with the build worker and mesh jobs (they may outlive a frame).
    struct Shared
    {
        std::mutex mutex;
        std::vector<IslandFinder::Island> found; // worker -> main thread
        std::vector<std::shared_ptr<Piece>> pieces;
        // Our own edits: the worker marks them applied instead of searching.
        std::unordered_map<const JarSignedDistanceField *, std::shared_ptr<Piece>> cuts;
        std::unordered_map<const JarSignedDistanceField *, std::shared_ptr<std::atomic<bool>>> merges;
    };
    std::shared_ptr<Shared> _shared = std::make_shared<Shared>();
    // Bodies being merged: their edit, and whether it was applied.
    std::unordered_map<uint64_t, std::pair<Ref<JarVoxelBlockSdf>, std::shared_ptr<std::atomic<bool>>>> _merging;
    std::vector<uint64_t> _bodies; // instance ids, oldest first
    JarVoxelVolume *_volume = nullptr;

    void attach();
    void detach();
    void start_cut(JarVoxelVolume &volume, const IslandFinder::Island &island);
    void spawn(JarVoxelVolume &volume, Piece &piece);
    bool rests_on_volume(JarVoxelDebrisBody *body) const;
    void remove_body(JarVoxelDebrisBody *body);

  protected:
    static void _bind_methods();
    void _notification(int what);

  public:
    ~JarVoxelDebris();

    // VolumeExtension
    void process(JarVoxelVolume &volume, double delta) override;
    void edit_applied(JarVoxelVolume &volume, const ModifySettings &edit) override;
    void meshes_committed(JarVoxelVolume &volume) override;

    // Puts a piece back into the volume where it lies (also when merge is off).
    void merge_debris(JarVoxelDebrisBody *body);
    TypedArray<JarVoxelDebrisBody> get_bodies() const;

    bool get_enabled() const { return _enabled; }
    void set_enabled(bool value) { _enabled = value; }
    int get_max_size() const { return _maxSize; }
    void set_max_size(int value) { _maxSize = std::max(1, value); }
    bool get_merge() const { return _merge; }
    void set_merge(bool value) { _merge = value; }
    float get_merge_delay() const { return _mergeDelay; }
    void set_merge_delay(float value) { _mergeDelay = std::max(0.0f, value); }
    float get_density() const { return _density; }
    void set_density(float value) { _density = std::max(1.0f, value); }
    int get_max_bodies() const { return _maxBodies; }
    void set_max_bodies(int value) { _maxBodies = std::max(1, value); }
    uint32_t get_collision_layer() const { return _collisionLayer; }
    void set_collision_layer(uint32_t value) { _collisionLayer = value; }
    uint32_t get_collision_mask() const { return _collisionMask; }
    void set_collision_mask(uint32_t value) { _collisionMask = value; }
};

#endif // JAR_VOXEL_DEBRIS_H
