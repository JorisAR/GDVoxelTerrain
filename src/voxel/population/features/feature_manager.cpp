#include "feature_manager.h"
#include "job_system.h"
#include "voxel_lod.h"
#include "voxel_volume.h"
#include <chrono>
#include <cmath>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <thread>
#include <unordered_set>

namespace
{
constexpr float BudgetMicroseconds = 2000.0f;

float nearest_distance(const std::vector<glm::vec3> &viewers, const glm::vec3 &p)
{
    float best = std::numeric_limits<float>::infinity();
    for (const glm::vec3 &v : viewers)
        best = std::min(best, glm::distance(v, p));
    return best;
}

void tangent_basis(const glm::vec3 &up, glm::vec3 &t1, glm::vec3 &t2)
{
    t1 = glm::normalize(glm::cross(up, std::abs(up.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1)));
    t2 = glm::cross(up, t1);
}
} // namespace

FeatureManager::FeatureManager(JarVoxelVolume &volume) : _volume(volume), _shared(std::make_shared<Shared>())
{
    refresh_features();
}

FeatureManager::~FeatureManager()
{
    // Jobs only touch _shared (kept alive by them) and their own copies, but
    // wait so none outlives the extension's resources.
    while (_shared->activeJobs > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void FeatureManager::refresh_features()
{
    _features.clear();
    _featureBiomes.clear();
    _range = 0.0f;
    for (const auto &entry : _volume.population_features())
    {
        _features.push_back(entry.item);
        _featureBiomes.push_back(entry.biome);
        if (entry.item.is_valid())
            _range = std::max(_range, entry.item->get_draw_distance());
    }
    _biomeMap = _volume.get_biome_map();
    _cellSize = _volume.get_feature_cell_size();
}

void FeatureManager::reset()
{
    for (auto &[key, cell] : _cells)
        free_groups(cell);
    _cells.clear();
    _toShow.clear();
    _scanViewers.clear();
    {
        // Results of running jobs belong to the old settings: drop them.
        std::lock_guard<std::mutex> lock(_shared->mutex);
        _shared->generated.clear();
        _shared->scanned.clear();
        _shared->scanDone = false;
    }
    _shared = std::make_shared<Shared>();
    _scanRunning = false;
    refresh_features();
}

Bounds FeatureManager::cell_bounds(const glm::ivec3 &key) const
{
    const glm::vec3 lo = glm::vec3(key) * _cellSize;
    return Bounds(lo, lo + glm::vec3(_cellSize));
}

bool FeatureManager::is_removed(const glm::vec3 &position) const
{
    for (const Bounds &b : _removed)
        if (b.contains_point(position))
            return true;
    return false;
}

void FeatureManager::remove_in(const Bounds &bounds)
{
    _removed.push_back(bounds);
    for (auto &[key, cell] : _cells)
    {
        if (!cell.ready || !cell_bounds(key).expanded(_cellSize).intersects(bounds))
            continue;
        bool changed = false;
        for (auto &transforms : cell.perFeature)
        {
            const size_t before = transforms.size();
            transforms.erase(std::remove_if(transforms.begin(), transforms.end(),
                                            [&](const Transform3D &t) {
                                                return bounds.contains_point(
                                                    glm::vec3(t.origin.x, t.origin.y, t.origin.z));
                                            }),
                             transforms.end());
            changed = changed || transforms.size() != before;
        }
        if (changed)
        {
            free_groups(cell);
            _toShow.push_back(key);
        }
    }
}

std::vector<FeatureManager::Instance> FeatureManager::instances_near(const glm::vec3 &position, float radius) const
{
    std::vector<Instance> result;
    for (const auto &[key, cell] : _cells)
    {
        if (!cell.ready)
            continue;
        for (size_t f = 0; f < cell.perFeature.size(); ++f)
            for (const Transform3D &t : cell.perFeature[f])
                if (glm::distance(glm::vec3(t.origin.x, t.origin.y, t.origin.z), position) <= radius)
                    result.push_back({int(f), t});
    }
    return result;
}

void FeatureManager::process()
{
    if (_features.empty() || _range <= 0.0f)
        return;
    const std::vector<glm::vec3> viewers = JarVoxelLoD::gather_viewers(_volume);
    apply_results();

    // Rescan once a viewer moved a fraction of a cell.
    bool moved = viewers.size() != _scanViewers.size();
    for (size_t i = 0; !moved && i < viewers.size(); ++i)
        moved = glm::distance(viewers[i], _scanViewers[i]) > _cellSize * 0.25f;
    if (moved && !_scanRunning)
    {
        _scanViewers = viewers;
        start_scan(viewers);
        _dirtyVisibility = true;
    }
    if (_dirtyVisibility)
    {
        _dirtyVisibility = false;
        update_visibility(viewers);
    }

    // Build pending groups within the frame budget.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::microseconds(static_cast<int64_t>(BudgetMicroseconds));
    while (!_toShow.empty() && std::chrono::steady_clock::now() < deadline)
    {
        const glm::ivec3 key = _toShow.back();
        _toShow.pop_back();
        auto it = _cells.find(key);
        if (it != _cells.end() && it->second.ready)
            build_groups(key, it->second, viewers);
    }
}

void FeatureManager::start_scan(const std::vector<glm::vec3> &viewers)
{
    Ref<JarSignedDistanceField> sdf = _volume.get_sdf();
    if (sdf.is_null())
        return;
    _scanRunning = true;
    std::shared_ptr<Shared> shared = _shared;
    shared->activeJobs++;
    const float range = _range, cellSize = _cellSize;
    JobSystem::get_pool().enqueue([shared, viewers, range, cellSize, sdf]() {
        std::vector<glm::ivec3> keys = scan_cells(viewers, range, cellSize, sdf);
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->scanned = std::move(keys);
            shared->scanDone = true;
        }
        shared->activeJobs--;
    });
}

void FeatureManager::start_generation(const glm::ivec3 &key)
{
    Ref<JarSignedDistanceField> sdf = _volume.get_sdf();
    if (sdf.is_null())
        return;
    std::shared_ptr<Shared> shared = _shared;
    shared->activeJobs++;
    const float cellSize = _cellSize;
    const std::vector<Ref<JarTerrainFeature>> features = _features;
    const std::vector<int> featureBiomes = _featureBiomes;
    const Ref<JarBiomeMap> biomes = _biomeMap;
    const UpField up = _volume.get_up_field();
    JobSystem::get_pool().enqueue([shared, key, cellSize, features, featureBiomes, biomes, sdf, up]() {
        std::vector<Instance> instances = generate_cell(key, cellSize, features, featureBiomes, biomes, sdf, up);
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            shared->generated.emplace_back(key, std::move(instances));
        }
        shared->activeJobs--;
    });
}

void FeatureManager::apply_results()
{
    std::vector<glm::ivec3> scanned;
    bool scanDone = false;
    std::vector<std::pair<glm::ivec3, std::vector<Instance>>> generated;
    {
        std::lock_guard<std::mutex> lock(_shared->mutex);
        scanDone = _shared->scanDone;
        _shared->scanDone = false;
        scanned.swap(_shared->scanned);
        generated.swap(_shared->generated);
    }

    for (auto &[key, instances] : generated)
    {
        auto it = _cells.find(key);
        if (it == _cells.end())
            continue; // left range while generating
        Cell &cell = it->second;
        cell.perFeature.assign(_features.size(), {});
        // Removals recorded after (or while) the job ran apply here too.
        std::vector<Bounds> removals;
        const Bounds bounds = cell_bounds(key).expanded(_cellSize);
        for (const Bounds &b : _removed)
            if (b.intersects(bounds))
                removals.push_back(b);
        for (const Instance &instance : instances)
        {
            const glm::vec3 p(instance.transform.origin.x, instance.transform.origin.y, instance.transform.origin.z);
            bool removed = false;
            for (const Bounds &b : removals)
                removed = removed || b.contains_point(p);
            if (!removed)
                cell.perFeature[instance.feature].push_back(instance.transform);
        }
        cell.groups.assign(_features.size(), nullptr);
        cell.ready = true;
        _toShow.push_back(key);
    }

    if (scanDone)
    {
        _scanRunning = false;
        std::unordered_set<glm::ivec3> wanted(scanned.begin(), scanned.end());
        // Drop cells that are no longer wanted and are well out of range.
        for (auto it = _cells.begin(); it != _cells.end();)
        {
            const Bounds b = cell_bounds(it->first);
            if (!wanted.count(it->first) && nearest_distance(_scanViewers, b.get_center()) > _range + 2.0f * _cellSize)
            {
                free_groups(it->second);
                it = _cells.erase(it);
            }
            else
                ++it;
        }
        // Nearest first, so features appear around the player before the horizon.
        std::sort(scanned.begin(), scanned.end(), [&](const glm::ivec3 &a, const glm::ivec3 &b) {
            return nearest_distance(_scanViewers, cell_bounds(a).get_center()) <
                   nearest_distance(_scanViewers, cell_bounds(b).get_center());
        });
        for (const glm::ivec3 &key : scanned)
            if (_cells.emplace(key, Cell()).second)
                start_generation(key);
        _dirtyVisibility = true;
    }
}

// Shows each feature's group where the cell is within that feature's draw
// distance, hides it beyond (with a cell of slack to avoid flicker).
void FeatureManager::update_visibility(const std::vector<glm::vec3> &viewers)
{
    for (auto &[key, cell] : _cells)
    {
        if (!cell.ready)
            continue;
        const float d = nearest_distance(viewers, cell_bounds(key).get_center());
        bool needsBuild = false;
        for (size_t f = 0; f < _features.size() && f < cell.groups.size(); ++f)
        {
            if (_features[f].is_null() || cell.perFeature[f].empty())
                continue;
            const float range = _features[f]->get_draw_distance();
            if (cell.groups[f] == nullptr && d < range + _cellSize)
                needsBuild = true;
            else if (cell.groups[f] != nullptr && d > range + 2.0f * _cellSize)
            {
                cell.groups[f]->queue_free();
                cell.groups[f] = nullptr;
            }
        }
        if (needsBuild)
            _toShow.push_back(key);
    }
}

void FeatureManager::build_groups(const glm::ivec3 &key, Cell &cell, const std::vector<glm::vec3> &viewers)
{
    const float d = nearest_distance(viewers, cell_bounds(key).get_center());
    for (size_t f = 0; f < _features.size() && f < cell.groups.size(); ++f)
    {
        const Ref<JarTerrainFeature> &feature = _features[f];
        if (feature.is_null() || cell.perFeature[f].empty() || cell.groups[f] != nullptr ||
            d >= feature->get_draw_distance() + _cellSize)
            continue;
        Node3D *group = memnew(Node3D);
        const std::vector<Transform3D> &transforms = cell.perFeature[f];
        if (feature->get_scene().is_valid())
        {
            for (const Transform3D &t : transforms)
            {
                Node *node = feature->get_scene()->instantiate();
                if (Node3D *n3 = Object::cast_to<Node3D>(node))
                    n3->set_transform(t);
                group->add_child(node);
            }
        }
        else if (feature->get_mesh().is_valid())
        {
            Ref<MultiMesh> multimesh;
            multimesh.instantiate();
            multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
            multimesh->set_mesh(feature->get_mesh());
            multimesh->set_instance_count(int(transforms.size()));
            for (size_t i = 0; i < transforms.size(); ++i)
                multimesh->set_instance_transform(int(i), transforms[i]);
            MultiMeshInstance3D *mmi = memnew(MultiMeshInstance3D);
            mmi->set_multimesh(multimesh);
            if (feature->get_material().is_valid())
                mmi->set_material_override(feature->get_material());
            mmi->set_cast_shadows_setting(feature->get_cast_shadows() ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON
                                                                      : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
            mmi->set_visibility_range_end(feature->get_draw_distance() + _cellSize);
            group->add_child(mmi);
        }
        _volume.add_child(group);
        cell.groups[f] = group;
    }
}

void FeatureManager::free_groups(Cell &cell)
{
    for (Node3D *&group : cell.groups)
        if (group != nullptr)
        {
            group->queue_free();
            group = nullptr;
        }
}

std::vector<glm::ivec3> FeatureManager::scan_cells(const std::vector<glm::vec3> &viewers, float range, float cellSize,
                                                   const Ref<JarSignedDistanceField> &sdf)
{
    // Coarse blocks of 4^3 cells first: whole blocks far from the surface are
    // skipped with one sample. SDFs from noise are not exact distances, hence
    // the generous margins.
    constexpr int Block = 4;
    constexpr float Margin = 2.0f;
    const float blockSize = cellSize * Block;
    std::unordered_set<glm::ivec3> keys;
    for (const glm::vec3 &v : viewers)
    {
        const glm::ivec3 lo = glm::ivec3(glm::floor((v - range) / blockSize));
        const glm::ivec3 hi = glm::ivec3(glm::floor((v + range) / blockSize));
        for (int bz = lo.z; bz <= hi.z; ++bz)
            for (int by = lo.y; by <= hi.y; ++by)
                for (int bx = lo.x; bx <= hi.x; ++bx)
                {
                    const glm::vec3 blockCenter = (glm::vec3(bx, by, bz) + 0.5f) * blockSize;
                    const float blockRadius = blockSize * 0.8660254f;
                    if (glm::distance(blockCenter, v) > range + blockRadius)
                        continue;
                    if (std::abs(sdf->distance(blockCenter)) > blockRadius * Margin)
                        continue;
                    for (int z = 0; z < Block; ++z)
                        for (int y = 0; y < Block; ++y)
                            for (int x = 0; x < Block; ++x)
                            {
                                const glm::ivec3 key = glm::ivec3(bx, by, bz) * Block + glm::ivec3(x, y, z);
                                const glm::vec3 center = (glm::vec3(key) + 0.5f) * cellSize;
                                const float cellRadius = cellSize * 0.8660254f;
                                if (glm::distance(center, v) > range + cellRadius)
                                    continue;
                                if (std::abs(sdf->distance(center)) > cellRadius * Margin)
                                    continue;
                                keys.insert(key);
                            }
                }
    }
    return std::vector<glm::ivec3>(keys.begin(), keys.end());
}

std::vector<FeatureManager::Instance> FeatureManager::generate_cell(
    const glm::ivec3 &key, float cellSize, const std::vector<Ref<JarTerrainFeature>> &features,
    const std::vector<int> &featureBiomes, const Ref<JarBiomeMap> &biomes, const Ref<JarSignedDistanceField> &sdf,
    const UpField &upField)
{
    std::vector<Instance> result;
    std::vector<glm::vec3> placed;    // positions of result
    std::vector<float> placedFootprint;

    const glm::vec3 lo = glm::vec3(key) * cellSize;
    const glm::vec3 hi = lo + glm::vec3(cellSize);
    const glm::vec3 center = (lo + hi) * 0.5f;
    const glm::vec3 up = upField.up(center);
    glm::vec3 t1, t2;
    tangent_basis(up, t1, t2);
    // The sampled square must cover the cell seen from any up direction, and
    // the march must cross it: both are the cube's diagonal. Samples landing
    // outside the cell are dropped (the neighbour owns them), which keeps the
    // density per area right.
    const float side = cellSize * 1.7320508f;

    // Each feature is its own layer, in list order: earlier features claim
    // their footprint first (trees, then rocks between them, then bushes...).
    for (int f = 0; f < int(features.size()); ++f)
    {
        if (features[f].is_null() || features[f]->get_density() <= 0.0f)
            continue;
        const JarTerrainFeature &feature = **features[f];
        const float density = feature.get_density();
        const float footprint = feature.get_footprint();

        // Poisson disc samples by dart throwing on a grid (one sample per grid
        // cell): even spacing within the layer, no clumps of overlapping copies.
        const float radius = std::max(footprint, 0.6f / std::sqrt(density));
        const int target = std::min(20000, int(std::ceil(density * side * side)));
        const float gridStep = radius / 1.4142136f;
        const int gridSize = std::max(1, int(std::ceil(side / gridStep)));
        if (gridSize > 2048)
            continue; // density too high for features; use details instead
        std::vector<int> grid(size_t(gridSize) * gridSize, -1);
        std::vector<glm::vec2> samples;
        PopulationRandom rng(PopulationRandom::hash(uint32_t(key.x), uint32_t(key.y), uint32_t(key.z), 0x5eedu + f));
        for (int attempt = 0; attempt < target * 8 && int(samples.size()) < target; ++attempt)
        {
            const glm::vec2 p(rng.next() * side, rng.next() * side);
            const int gx = std::min(gridSize - 1, int(p.x / gridStep));
            const int gy = std::min(gridSize - 1, int(p.y / gridStep));
            bool ok = true;
            for (int y = std::max(0, gy - 2); ok && y <= std::min(gridSize - 1, gy + 2); ++y)
                for (int x = std::max(0, gx - 2); ok && x <= std::min(gridSize - 1, gx + 2); ++x)
                {
                    const int s = grid[size_t(y) * gridSize + x];
                    ok = s < 0 || glm::distance(samples[s], p) >= radius;
                }
            if (!ok)
                continue;
            grid[size_t(gy) * gridSize + gx] = int(samples.size());
            samples.push_back(p);
        }

        for (const glm::vec2 &s : samples)
        {
            // Every sample consumes the same random numbers, so its look does
            // not depend on whether earlier samples were kept.
            const float keep = rng.next(), r1 = rng.next(), r2 = rng.next();
            const glm::vec3 onPlane = center + t1 * (s.x - side * 0.5f) + t2 * (s.y - side * 0.5f);
            // Mask and clusters only depend on the position: test them on the
            // sample's projection before paying for the ray march.
            const int biome = f < int(featureBiomes.size()) ? featureBiomes[f] : -1;
            // Biome features grow where their biome is, fading with its weight.
            const auto biome_weight = [&](const glm::vec3 &at) {
                return biome < 0 || biomes.is_null() ? 1.0f : biomes->evaluate(at, upField).weight_of(biome);
            };
            const float spread = feature.distribution(onPlane, up) * biome_weight(onPlane);
            if (keep >= spread)
                continue;
            glm::vec3 p = onPlane + up * (side * 0.5f);
            float d = sdf->distance(p);
            if (d < 0.0f)
                continue; // surface above the sampled square: not this cell's
            float travelled = 0.0f;
            bool hit = false;
            for (int step = 0; step < 128 && travelled <= side; ++step)
            {
                if (d < 0.02f)
                {
                    hit = true;
                    break;
                }
                const float advance = std::max(d * 0.7f, 0.02f); // noise SDFs overestimate a little
                p -= up * advance;
                travelled += advance;
                d = sdf->distance(p);
            }
            if (!hit || glm::any(glm::lessThan(p, lo)) || glm::any(glm::greaterThanEqual(p, hi)))
                continue;
            const glm::vec3 normal = sdf->normal(p);
            const glm::vec3 localUp = upField.up(p);
            // Ground fit, and the mask at the exact position (the projection
            // above was only an estimate): keep with the combined probability.
            const float fit = feature.terrain_fit(upField.height(p), normal, localUp);
            const float exact = feature.distribution(p, localUp) * biome_weight(p);
            if (keep >= fit * exact)
                continue;
            bool blocked = false;
            for (size_t i = 0; i < placed.size() && !blocked; ++i)
                blocked = glm::distance(placed[i], p) < std::max(footprint, placedFootprint[i]);
            if (blocked)
                continue;
            placed.push_back(p);
            placedFootprint.push_back(footprint);
            result.push_back({f, feature.build_transform(p, localUp, normal, r1, r2)});
        }
    }
    return result;
}
