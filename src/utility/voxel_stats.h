#ifndef JAR_VOXEL_STATS_H
#define JAR_VOXEL_STATS_H

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>

using namespace godot;

// Where the terrain spends its time, per frame, across all volumes: main
// thread stages (what a frame waits for) and worker stages (summed over
// threads). Scopes nest: a stage's time excludes the stages timed inside it,
// so main thread stages add up to the terrain's share of the frame.
//
// Shown live in the editor (Debugger > Monitors, "JarVoxel ..."), as the
// average and the worst frame of each second (monitors are sampled about
// once a second, so a per-frame value would miss the spikes), and to scripts
// through JarVoxelStats (benchmarks).
namespace VoxelStats
{
enum Stage
{
    // Main thread.
    MainVolume,     // JarVoxelVolume::process, minus the stages below
    MainUpload,     // mesh arrays to the GPU
    MainCommit,     // finished meshes swapped in
    MainColliders,  // collision shapes handed to physics
    MainChunkCreate, // chunk scenes instantiated (pool empty)
    MainEditWait,   // waiting for an edit to be applied and meshed
    MainDetails,    // grass and flowers
    MainFeatures,   // trees and rocks
    MainExtensions, // debris and other volume extensions
    MainUniverse,   // floating origin, frames, gravity
    MainLayers,     // atmospheres, clouds, water, lights
    // Workers.
    WorkerBuild,        // octree builds (LOD changes)
    WorkerEdit,         // edits applied to the octree
    WorkerMesh,         // chunk meshing
    WorkerCollider,     // collision data (part of meshing)
    WorkerExtension,    // extension work after edits (debris search)
    WorkerDetails,      // grass and flower placement
    WorkerFeatures,     // tree and rock placement
    StageCount
};

enum Count
{
    ChunksMeshed,
    ChunksCommitted,
    ChunksCreated,
    CollidersBuilt,
    CountCount
};

enum Gauge
{
    MeshQueue,     // chunks waiting for or being meshed
    ColliderQueue, // colliders waiting for the main thread
    GaugeCount
};

void add_time(Stage stage, int64_t nanoseconds);
void add_count(Count count, int64_t amount = 1);
void set_gauge(Gauge gauge, int64_t value);

// Called by every volume at the start of its frame: the first call of a
// frame closes the previous one.
void tick(uint64_t frame);

// Times its lifetime into `stage`, minus nested scopes on the same thread.
class Scope
{
  public:
    explicit Scope(Stage stage);
    ~Scope();
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

  private:
    Stage _stage;
    std::chrono::steady_clock::time_point _start;
    int64_t _nested = 0; // ns spent in scopes inside this one
    Scope *_parent;
};

// The editor's monitors (once; safe to call again).
void register_monitors();
void unregister_monitors();
} // namespace VoxelStats

// Script access, e.g. for benchmarks: JarVoxelStats.get_frame().
class JarVoxelStats : public Object
{
    GDCLASS(JarVoxelStats, Object);

  protected:
    static void _bind_methods();

  public:
    // The last finished frame: milliseconds per stage, counts and gauges.
    static Dictionary get_frame();
    // The last full second: per stage [average, worst] milliseconds per frame.
    static Dictionary get_second();
};

#endif // JAR_VOXEL_STATS_H
