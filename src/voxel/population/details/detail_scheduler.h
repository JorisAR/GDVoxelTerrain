#ifndef JAR_DETAIL_SCHEDULER_H
#define JAR_DETAIL_SCHEDULER_H

#include "chunk_detail_generator.h"
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

class JarVoxelVolume;
class JarVoxelChunk;

// Details (grass, flowers, pebbles) only matter near a viewer: they are drawn
// within their draw distance. They are generated for full-detail chunks within
// that distance only, nearest first, on one background thread of their own
// (not with the meshing), and dropped again from chunks the viewer left.
class DetailScheduler
{
  public:
    explicit DetailScheduler(JarVoxelVolume &volume);
    ~DetailScheduler(); // stops the thread (waits for the job it is running)

    // Main thread, every frame: show finished details, queue the chunks that
    // need some, drop them from chunks out of range.
    void process();
    // Main thread. A full-detail chunk with new mesh data (or none any more).
    void track(JarVoxelChunk *chunk);
    void untrack(JarVoxelChunk *chunk);
    // Details or their settings changed: regenerate (or remove) everywhere.
    void reset();
    // Chunks in range still wait for (or are getting) details.
    bool busy() const { return _inFlight > 0 || _waiting; }

  private:
    struct Job
    {
        JarVoxelChunk *chunk;
        uint64_t token;
        DetailInputs inputs;
        std::shared_ptr<const std::vector<Ref<JarTerrainDetail>>> details;
        std::shared_ptr<const std::vector<int>> biomes;
        Ref<JarBiomeMap> biomeMap;
        std::shared_ptr<const GroundPalette> ground;
        UpField up;
    };
    struct Result
    {
        JarVoxelChunk *chunk;
        uint64_t token;
        std::shared_ptr<const std::vector<Ref<JarTerrainDetail>>> details;
        std::vector<PackedFloat32Array> buffers;
    };

    void worker();
    void refresh_details();
    float range() const; // meters from a viewer within which chunks get details

    JarVoxelVolume &_volume;
    std::unordered_set<JarVoxelChunk *> _chunks; // full-detail chunks with mesh data
    std::shared_ptr<const std::vector<Ref<JarTerrainDetail>>> _details;
    std::shared_ptr<const std::vector<int>> _detailBiomes;
    float _maxDrawDistance = 0.0f; // 0: no limit
    bool _detailsDirty = true;
    int _inFlight = 0;
    bool _waiting = false; // chunks in range without details at the last process()

    std::mutex _mutex;
    std::condition_variable _wake;
    std::deque<Job> _jobs;
    std::vector<Result> _results;
    bool _stop = false;
    std::thread _thread;

    // Few jobs at a time: the nearest chunks are chosen again every frame
    // while the viewer moves.
    static constexpr int MaxInFlight = 4;
    static constexpr int64_t ApplyBudgetMicroseconds = 2000;
};

#endif // JAR_DETAIL_SCHEDULER_H
