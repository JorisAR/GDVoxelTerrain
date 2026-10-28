#include "feature_manager.h"
#include "job_system.h"
#include "voxel_lod.h"
#include "voxel_volume.h"
#include <chrono>
#include <cmath>
#include "population_rendering.h"
#include <functional>
#include <godot_cpp/classes/collision_object3d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
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
    _templates.clear();
    _range = 0.0f;
    for (const auto &entry : _volume.population_features())
    {
        _features.push_back(entry.item);
        _featureBiomes.push_back(entry.biome);
        _templates.push_back(make_template(entry.item));
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

bool FeatureManager::Removal::hits(const Transform3D &feature) const
{
    const glm::vec3 base(feature.origin.x, feature.origin.y, feature.origin.z);
    if (!bounds.contains_point(base))
        return false;
    if (!edit)
        return true;
    // The base, and a meter up the feature (material added over its foot).
    const Vector3 upward = feature.basis.get_column(1).normalized();
    for (const glm::vec3 &p : {base, base + glm::vec3(upward.x, upward.y, upward.z)})
    {
        const float d = edit->sample(p);
        switch (edit->operation)
        {
        case SDF::SDF_OPERATION_INTERSECTION:
        case SDF::SDF_OPERATION_SMOOTH_INTERSECTION:
            if (d > -reach) // the edit keeps only its inside
                return true;
            break;
        default: // union / subtraction: the shape itself changes the ground
            if (d < reach)
                return true;
            break;
        }
    }
    return false;
}

bool FeatureManager::is_removed(const glm::vec3 &position) const
{
    const Transform3D at(Basis(), Vector3(position.x, position.y, position.z));
    for (const Removal &r : _removed)
        if (r.hits(at))
            return true;
    return false;
}

void FeatureManager::remove_in(const Bounds &bounds)
{
    apply_removal({bounds, nullptr, 0.0f});
}

void FeatureManager::remove_edited(const ModifySettings &edit, float reach)
{
    apply_removal({edit.bounds.expanded(reach + 1.0f), std::make_shared<ModifySettings>(edit), reach});
}

void FeatureManager::apply_removal(const Removal &removal)
{
    _removed.push_back(removal);
    for (auto &[key, cell] : _cells)
    {
        if (!cell.ready || !cell_bounds(key).expanded(_cellSize).intersects(removal.bounds))
            continue;
        bool changed = false;
        for (auto &transforms : cell.perFeature)
        {
            const size_t before = transforms.size();
            transforms.erase(std::remove_if(transforms.begin(), transforms.end(),
                                            [&](const Placed &p) { return removal.hits(p.transform); }),
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
            for (const Placed &p : cell.perFeature[f])
            {
                const Vector3 o = p.transform.origin;
                if (glm::distance(glm::vec3(o.x, o.y, o.z), position) <= radius)
                    result.push_back({int(f), p.transform, p.ground});
            }
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
    JobSystem::get_pool().enqueue_background([shared, viewers, range, cellSize, sdf]() {
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
    const std::shared_ptr<const GroundPalette> ground = _volume.get_ground_palette();
    JobSystem::get_pool().enqueue_background([shared, key, cellSize, features, featureBiomes, biomes, sdf, up, ground]() {
        std::vector<Instance> instances =
            generate_cell(key, cellSize, features, featureBiomes, biomes, sdf, up, ground);
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
        std::vector<const Removal *> removals;
        const Bounds bounds = cell_bounds(key).expanded(_cellSize);
        for (const Removal &r : _removed)
            if (r.bounds.intersects(bounds))
                removals.push_back(&r);
        for (const Instance &instance : instances)
        {
            const glm::vec3 p(instance.transform.origin.x, instance.transform.origin.y, instance.transform.origin.z);
            bool removed = false;
            for (const Removal *r : removals)
                removed = removed || r->hits(instance.transform);
            if (!removed)
                cell.perFeature[instance.feature].push_back({instance.transform, instance.ground});
        }
        cell.groups.assign(_features.size(), nullptr);
        cell.colliders.assign(_features.size(), nullptr);
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
            // Colliders only near viewers.
            const float reach = _templates[f].colliders.empty() || !_volume.colliders_enabled()
                                    ? 0.0f
                                    : _features[f]->get_collision_distance();
            if (cell.colliders[f] == nullptr && reach > 0.0f && d < reach + _cellSize)
                needsBuild = true;
            else if (cell.colliders[f] != nullptr && d > reach + 2.0f * _cellSize)
            {
                cell.colliders[f]->queue_free();
                cell.colliders[f] = nullptr;
            }
        }
        if (needsBuild)
            _toShow.push_back(key);
    }
}

// A feature's scene read into parts: its MeshInstance3Ds (with visibility
// ranges) and collision shapes, relative to the scene root.
FeatureManager::Template FeatureManager::make_template(const Ref<JarTerrainFeature> &feature)
{
    Template t;
    if (feature.is_null())
        return t;
    const Ref<PackedScene> scene = feature->get_scene();
    if (scene.is_null())
    {
        if (feature->get_mesh().is_valid())
            t.parts.push_back({feature->get_mesh(), feature->get_material(), Transform3D(), 0.0f, 0.0f,
                               feature->get_cast_shadows() ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON
                                                           : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF});
        return t;
    }
    if (feature->get_scene_mode() == JarTerrainFeature::SCENE_MODE_NODES)
    {
        t.nodes = true;
        return t;
    }
    Node *root = scene->instantiate();
    if (root == nullptr)
        return t;
    bool haveBody = false;
    std::function<void(Node *, const Transform3D &)> visit = [&](Node *node, const Transform3D &xform) {
        if (MeshInstance3D *mesh = Object::cast_to<MeshInstance3D>(node))
        {
            if (mesh->get_mesh().is_valid())
            {
                Ref<Material> material = mesh->get_material_override();
                if (material.is_null() && mesh->get_surface_override_material_count() > 0)
                    material = mesh->get_surface_override_material(0);
                t.parts.push_back({mesh->get_mesh(), material, xform, mesh->get_visibility_range_begin(),
                                   mesh->get_visibility_range_end(), mesh->get_cast_shadows_setting()});
            }
        }
        else if (CollisionShape3D *shape = Object::cast_to<CollisionShape3D>(node))
        {
            if (!shape->is_disabled() && shape->get_shape().is_valid())
            {
                t.colliders.push_back({shape->get_shape(), xform});
                CollisionObject3D *body = Object::cast_to<CollisionObject3D>(shape->get_parent());
                if (body != nullptr && !haveBody)
                {
                    t.layer = body->get_collision_layer();
                    t.mask = body->get_collision_mask();
                    haveBody = true;
                }
            }
        }
        for (int i = 0; i < node->get_child_count(); ++i)
        {
            Node *child = node->get_child(i);
            const Node3D *child3d = Object::cast_to<Node3D>(child);
            // Hidden parts are not drawn (collision shapes count regardless).
            if (child3d != nullptr && !child3d->is_visible() && Object::cast_to<CollisionShape3D>(child) == nullptr)
                continue;
            visit(child, child3d != nullptr ? xform * child3d->get_transform() : xform);
        }
    };
    visit(root, Transform3D()); // the root's own transform does not count
    memdelete(root);
    return t;
}

Node3D *FeatureManager::build_colliders(const Template &t, const std::vector<Placed> &placed) const
{
    StaticBody3D *body = memnew(StaticBody3D);
    body->set_collision_layer(t.layer);
    body->set_collision_mask(t.mask);
    for (const Placed &p : placed)
        for (const Template::Collider &c : t.colliders)
        {
            const uint32_t owner = body->create_shape_owner(body);
            body->shape_owner_set_transform(owner, p.transform * c.local);
            body->shape_owner_add_shape(owner, c.shape);
        }
    return body;
}

void FeatureManager::build_groups(const glm::ivec3 &key, Cell &cell, const std::vector<glm::vec3> &viewers)
{
    const float d = nearest_distance(viewers, cell_bounds(key).get_center());
    const float margin = 0.87f * _cellSize; // instances lie within this of the cell center
    for (size_t f = 0; f < _features.size() && f < cell.groups.size(); ++f)
    {
        const Ref<JarTerrainFeature> &feature = _features[f];
        if (feature.is_null() || cell.perFeature[f].empty())
            continue;
        const Template &tpl = _templates[f];
        const std::vector<Placed> &placed = cell.perFeature[f];
        const float reach =
            tpl.colliders.empty() || !_volume.colliders_enabled() ? 0.0f : feature->get_collision_distance();
        if (cell.colliders[f] == nullptr && reach > 0.0f && d < reach + _cellSize)
        {
            cell.colliders[f] = build_colliders(tpl, placed);
            _volume.add_child(cell.colliders[f]);
        }
        const float draw = feature->get_draw_distance();
        if (cell.groups[f] != nullptr || d >= draw + _cellSize)
            continue;
        Node3D *group = memnew(Node3D);
        if (tpl.nodes)
        {
            for (const Placed &p : placed)
            {
                Node *node = feature->get_scene()->instantiate();
                if (Node3D *n3 = Object::cast_to<Node3D>(node))
                    n3->set_transform(p.transform);
                group->add_child(node);
            }
        }
        for (const Template::Part &part : tpl.parts)
        {
            const float end = part.end > 0.0f ? std::min(part.end, draw) : draw;
            if (part.begin >= end)
                continue;
            // Transform, then custom data: the ground color (see jar_population.gdshaderinc).
            PackedFloat32Array buffer;
            buffer.resize(int64_t(placed.size()) * 16);
            float *w = buffer.ptrw();
            for (const Placed &p : placed)
            {
                const Transform3D t = p.transform * part.local;
                for (int row = 0; row < 3; ++row)
                {
                    *w++ = t.basis.rows[row].x;
                    *w++ = t.basis.rows[row].y;
                    *w++ = t.basis.rows[row].z;
                    *w++ = t.origin[row];
                }
                PopulationRendering::write_custom(w, p.ground, part.begin, end);
                w += 4;
            }
            Ref<MultiMesh> multimesh;
            multimesh.instantiate();
            multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
            multimesh->set_use_custom_data(true);
            multimesh->set_mesh(part.mesh);
            multimesh->set_instance_count(int(placed.size()));
            multimesh->set_buffer(buffer);
            MultiMeshInstance3D *mmi = memnew(MultiMeshInstance3D);
            mmi->set_multimesh(multimesh);
            if (part.material.is_valid())
                mmi->set_material_override(part.material);
            mmi->set_cast_shadows_setting(part.shadows);
            PopulationRendering::set_lod_range(mmi, PopulationRendering::effective_material(part.mesh, part.material),
                                               part.begin, end, margin);
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
    for (Node3D *&body : cell.colliders)
        if (body != nullptr)
        {
            body->queue_free();
            body = nullptr;
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

namespace
{
// Distance down from `top` (outside, at distance `d`) to the first surface
// within `range`, or -1. Secant steps along the ray (an SDF changes almost
// linearly along it near a surface, even where it is not an exact distance),
// kept between 0.7 and 3 times the distance so they neither crawl nor jump
// far through thin parts; once past the surface, false position (Illinois)
// within the bracket. A few samples instead of a dozen or more.
float find_surface_below(const JarSignedDistanceField &sdf, const glm::vec3 &top, const glm::vec3 &up, float d,
                         float range)
{
    constexpr float Tolerance = 0.02f;
    auto sample = [&](float t) { return sdf.distance(top - up * t); };
    // Outside: secant steps (the first one conservative).
    float a = 0.0f, fa = d, prev = 0.0f, fPrev = d;
    float b = -1.0f, fb = 0.0f;
    for (int step = 0; step < 48; ++step)
    {
        if (fa < Tolerance)
            return a;
        const float secant = step > 0 && fPrev > fa ? fa * (a - prev) / (fPrev - fa) : 0.7f * fa;
        const float next = a + std::clamp(secant, std::max(0.7f * fa, Tolerance), 3.0f * fa);
        if (next > range)
            return -1.0f;
        const float fn = sample(next);
        if (fn < 0.0f)
        {
            b = next;
            fb = fn;
            break;
        }
        prev = a;
        fPrev = fa;
        a = next;
        fa = fn;
    }
    if (b < 0.0f)
        return -1.0f;
    // Bracketed: false position, Illinois variant (halve the end that stays).
    int side = 0;
    for (int step = 0; step < 24; ++step)
    {
        if (-fb < Tolerance)
            return b;
        if (fa < Tolerance)
            return a;
        const float c = (a * fb - b * fa) / (fb - fa);
        const float fc = sample(c);
        if (fc < 0.0f)
        {
            b = c;
            fb = fc;
            if (side == -1)
                fa *= 0.5f;
            side = -1;
        }
        else
        {
            a = c;
            fa = fc;
            if (side == 1)
                fb *= 0.5f;
            side = 1;
        }
    }
    return std::abs(fa) < std::abs(fb) ? a : b;
}
} // namespace

std::vector<FeatureManager::Instance> FeatureManager::generate_cell(
    const glm::ivec3 &key, float cellSize, const std::vector<Ref<JarTerrainFeature>> &features,
    const std::vector<int> &featureBiomes, const Ref<JarBiomeMap> &biomes, const Ref<JarSignedDistanceField> &sdf,
    const UpField &upField, const std::shared_ptr<const GroundPalette> &ground)
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

        // Whole layers that cannot place anything here are skipped before any
        // sampling: no cluster in reach, or the feature's biome is absent.
        const float halfDiagonal = cellSize * 0.8660254f;
        if (!feature.clusters_reach(center, up, halfDiagonal))
            continue;
        const int layerBiome = f < int(featureBiomes.size()) ? featureBiomes[f] : -1;
        if (layerBiome >= 0 && biomes.is_valid())
        {
            // Biomes change over hundreds of meters: a 3x3 grid over the cell finds them.
            float most = 0.0f;
            for (int y = -1; y <= 1 && most < 0.01f; ++y)
                for (int x = -1; x <= 1 && most < 0.01f; ++x)
                    most = std::max(most, biomes->evaluate(center + (t1 * float(x) + t2 * float(y)) * (cellSize * 0.5f), upField)
                                              .weight_of(layerBiome));
            if (most < 0.01f)
                continue;
        }

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
            // Where the sample's column (down along up) crosses the cell: a
            // column that misses it cannot place anything here (most of the
            // square does, it covers the cell from any up direction).
            const glm::vec3 top = onPlane + up * (side * 0.5f);
            float enter = 0.0f, leave = side;
            for (int a = 0; a < 3 && enter <= leave; ++a)
            {
                const float dir = -up[a];
                if (std::abs(dir) < 1e-6f)
                {
                    if (top[a] < lo[a] || top[a] >= hi[a])
                        leave = -1.0f;
                    continue;
                }
                const float ta = (lo[a] - top[a]) / dir, tb = (hi[a] - top[a]) / dir;
                enter = std::max(enter, std::min(ta, tb));
                leave = std::min(leave, std::max(ta, tb));
            }
            if (enter > leave)
                continue;
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
            // The first surface in the cell along the column (none if the
            // column enters the cell underground).
            const glm::vec3 entry = top - up * enter;
            float d = sdf->distance(entry);
            if (d < 0.0f)
                continue;
            const float t = find_surface_below(*sdf.ptr(), entry, up, d, leave - enter);
            if (t < 0.0f)
                continue;
            const glm::vec3 p = entry - up * t;
            if (glm::any(glm::lessThan(p, lo)) || glm::any(glm::greaterThanEqual(p, hi)))
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
            result.push_back({f, feature.build_transform(p, localUp, normal, r1, r2),
                              ground ? ground->color_at(p, upField) : glm::vec3(0.3f, 0.35f, 0.2f)});
        }
    }
    return result;
}
