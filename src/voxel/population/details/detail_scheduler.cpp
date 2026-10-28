#include "detail_scheduler.h"
#include "voxel_chunk.h"
#include "voxel_volume.h"
#include <algorithm>
#include <chrono>
#include <limits>

namespace
{
// Half a full-detail chunk's diagonal: its details reach that far from its center.
float chunk_reach(const JarVoxelVolume &volume)
{
    return 0.87f * float(volume.get_chunk_size()) * volume.get_voxel_size();
}
} // namespace

DetailScheduler::DetailScheduler(JarVoxelVolume &volume) : _volume(volume)
{
    _thread = std::thread([this] { worker(); });
}

DetailScheduler::~DetailScheduler()
{
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stop = true;
        _jobs.clear();
    }
    _wake.notify_all();
    if (_thread.joinable())
        _thread.join();
}

void DetailScheduler::worker()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _wake.wait(lock, [this] { return _stop || !_jobs.empty(); });
            if (_stop)
                return;
            job = std::move(_jobs.front());
            _jobs.pop_front();
        }
        ChunkDetailGenerator generator(job.up);
        std::vector<PackedFloat32Array> buffers =
            generator.generate_detail_buffers(*job.details, *job.biomes, job.biomeMap, job.ground, job.inputs);
        std::lock_guard<std::mutex> lock(_mutex);
        _results.push_back({job.chunk, job.token, job.details, std::move(buffers)});
    }
}

void DetailScheduler::track(JarVoxelChunk *chunk)
{
    _chunks.insert(chunk);
}

void DetailScheduler::untrack(JarVoxelChunk *chunk)
{
    if (_chunks.erase(chunk) > 0)
        chunk->clear_details();
}

void DetailScheduler::reset()
{
    for (JarVoxelChunk *chunk : _chunks)
        chunk->clear_details();
    _detailsDirty = true;
}

void DetailScheduler::refresh_details()
{
    auto details = std::make_shared<std::vector<Ref<JarTerrainDetail>>>();
    auto biomes = std::make_shared<std::vector<int>>();
    _maxDrawDistance = 0.0f;
    bool unlimited = false;
    for (const auto &entry : _volume.population_details())
    {
        details->push_back(entry.item);
        biomes->push_back(entry.biome);
        if (entry.item.is_null())
            continue;
        const float draw = entry.item->get_draw_distance();
        unlimited = unlimited || draw <= 0.0f;
        _maxDrawDistance = std::max(_maxDrawDistance, draw);
    }
    if (unlimited)
        _maxDrawDistance = 0.0f;
    _details = details;
    _detailBiomes = biomes;
    _detailsDirty = false;
}

float DetailScheduler::range() const
{
    if (_maxDrawDistance <= 0.0f)
        return std::numeric_limits<float>::max();
    return _maxDrawDistance + chunk_reach(_volume);
}

void DetailScheduler::process()
{
    // Finished jobs: show them, unless the chunk changed (or left) meanwhile.
    std::vector<Result> results;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        results.swap(_results);
    }
    for (Result &result : results)
    {
        _inFlight--;
        JarVoxelChunk *chunk = result.chunk;
        if (_chunks.count(chunk) == 0 || chunk->detail_token != result.token)
            continue;
        chunk->apply_details(_volume, *result.details, result.buffers);
    }

    if (_detailsDirty)
        refresh_details();
    _waiting = _detailsDirty;
    if (_chunks.empty())
        return;

    // Nearest chunks first; drop details a little beyond the range, so chunks
    // at its edge do not flip on and off.
    const float near = range();
    const float far = near + chunk_reach(_volume);
    std::vector<std::pair<float, JarVoxelChunk *>> wanted;
    for (JarVoxelChunk *chunk : _chunks)
    {
        const Vector3 p = chunk->get_position();
        const float distance = _volume.distance_to_nearest_viewer(glm::vec3(p.x, p.y, p.z));
        if (_details->empty() || distance > far)
        {
            if (chunk->has_details() || chunk->detail_state != JarVoxelChunk::DETAILS_NONE)
                chunk->clear_details(); // also cancels a job in flight (new token)
        }
        else if (distance <= near && chunk->detail_state == JarVoxelChunk::DETAILS_NONE)
            wanted.emplace_back(distance, chunk);
    }
    _waiting = !wanted.empty();
    if (wanted.empty() || _inFlight >= MaxInFlight)
        return;
    std::sort(wanted.begin(), wanted.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    const std::shared_ptr<const GroundPalette> ground = _volume.get_ground_palette();
    const Ref<JarBiomeMap> biomeMap = _volume.get_biome_map();
    const UpField up = _volume.get_up_field();
    std::lock_guard<std::mutex> lock(_mutex);
    for (const auto &[distance, chunk] : wanted)
    {
        if (_inFlight >= MaxInFlight)
            break;
        chunk->detail_state = JarVoxelChunk::DETAILS_QUEUED;
        _jobs.push_back({chunk, chunk->detail_token, chunk->detail_inputs, _details, _detailBiomes, biomeMap, ground, up});
        _inFlight++;
    }
    _wake.notify_one();
}
