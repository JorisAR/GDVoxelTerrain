#include "voxel_population.h"
#include "detail_scheduler.h"
#include "feature_manager.h"
#include "voxel_chunk.h"
#include "voxel_stats.h"
#include "voxel_volume.h"
#include <godot_cpp/variant/utility_functions.hpp>

JarVoxelPopulation::JarVoxelPopulation() = default;
JarVoxelPopulation::~JarVoxelPopulation() = default; // the managers wait for their jobs

void JarVoxelPopulation::_notification(int what)
{
    if (what == NOTIFICATION_ENTER_TREE)
        attach();
    else if (what == NOTIFICATION_EXIT_TREE)
        detach();
}

void JarVoxelPopulation::attach()
{
    JarVoxelVolume *volume = Object::cast_to<JarVoxelVolume>(get_parent());
    if (volume != _volume)
    {
        _details.reset();
        _features.reset();
    }
    _volume = volume;
    if (_volume == nullptr)
        return;
    _volume->add_extension(this);
    if (_details)
        return; // back in the tree: everything is still there
    // Added to a volume already built: its chunks need to be seen again.
    for (int i = 0; i < _volume->get_child_count(); ++i)
        if (Object::cast_to<JarVoxelChunk>(_volume->get_child(i)) != nullptr)
        {
            _volume->remesh_all();
            break;
        }
}

// The managers stay: a volume being re-parented keeps its removed features.
void JarVoxelPopulation::detach()
{
    if (_volume)
        _volume->remove_extension(this); // waits until no hook of ours runs
}

bool JarVoxelPopulation::ensure_running()
{
    if (_details)
        return true;
    if (_volume == nullptr || _volume->get_sdf().is_null())
        return false;
    _features = std::make_unique<FeatureManager>(*_volume, *this);
    _details = std::make_unique<DetailScheduler>(*_volume, *this);
    return true;
}

void JarVoxelPopulation::stop()
{
    if (_features)
        _features->reset(); // frees its nodes
    _features.reset();      // waits for its jobs
    _details.reset();       // waits for its thread
}

void JarVoxelPopulation::reset_features()
{
    if (_features)
        _features->reset();
}

void JarVoxelPopulation::reset_details()
{
    if (_details)
        _details->reset();
}

void JarVoxelPopulation::regenerate()
{
    reset_features();
    reset_details();
}

void JarVoxelPopulation::on_table_changed()
{
    regenerate();
}

void JarVoxelPopulation::set_table(const Ref<JarPopulationTable> &value)
{
    const Callable callback = callable_mp(this, &JarVoxelPopulation::on_table_changed);
    if (_table.is_valid() && _table->is_connected("changed", callback))
        _table->disconnect("changed", callback);
    _table = value;
    if (_table.is_valid())
        _table->connect("changed", callback, Object::CONNECT_DEFERRED);
    regenerate();
}

void JarVoxelPopulation::set_features_enabled(bool value)
{
    if (_featuresEnabled == value)
        return;
    _featuresEnabled = value;
    reset_features();
}

// Off: details disappear at once and are no longer generated. On: they come
// back near the viewers.
void JarVoxelPopulation::set_details_enabled(bool value)
{
    if (_detailsEnabled == value)
        return;
    _detailsEnabled = value;
    reset_details();
}

void JarVoxelPopulation::set_preview_features(bool value)
{
    if (_previewFeatures == value)
        return;
    _previewFeatures = value;
    if (_volume != nullptr && _volume->is_previewing())
        reset_features();
}

void JarVoxelPopulation::set_preview_details(bool value)
{
    if (_previewDetails == value)
        return;
    _previewDetails = value;
    if (_volume != nullptr && _volume->is_previewing())
        reset_details();
}

void JarVoxelPopulation::set_feature_cell_size(float value)
{
    _featureCellSize = std::max(1.0f, value);
    reset_features();
}

template <typename T> std::vector<JarVoxelPopulation::Entry<T>> JarVoxelPopulation::entries(bool enabled) const
{
    std::vector<Entry<T>> result;
    if (!enabled || _table.is_null() || _volume == nullptr)
        return result;
    const Ref<JarBiomeMap> map = _volume->get_biome_map();
    const TypedArray<JarBiome> biomes = map.is_valid() ? map->get_biomes() : TypedArray<JarBiome>();
    const TypedArray<Dictionary> rows = _table->get_rows();
    const String all = "*";
    for (int64_t r = 0; r < rows.size(); ++r)
    {
        const Dictionary row = rows[r];
        const Ref<T> item = row.get("asset", Variant());
        if (item.is_null())
            continue;
        const Dictionary grows = row.get("biomes", Dictionary());
        const Array names = grows.keys();
        // Everywhere: one layer, whatever the biomes are.
        if (names.size() == 1 && String(names[0]) == all)
        {
            const float density = grows[names[0]];
            if (density > 0.0f)
                result.push_back({item, {-1, density}});
            continue;
        }
        const float others = grows.get(all, 0.0f);
        int found = 0;
        for (int64_t b = 0; b < biomes.size(); ++b)
        {
            const Ref<JarBiome> biome = biomes[b];
            if (biome.is_null())
                continue;
            const String name = biome->get_name();
            found += grows.has(name) ? 1 : 0;
            const float density = grows.get(name, others);
            if (density > 0.0f)
                result.push_back({item, {int(b), density}});
        }
        if (found != int(names.size()) - (grows.has(all) ? 1 : 0))
            UtilityFunctions::push_warning("Population table row ", r, " names a biome that does not exist: ", names);
    }
    return result;
}

std::vector<JarVoxelPopulation::Entry<JarTerrainFeature>> JarVoxelPopulation::feature_entries() const
{
    const bool previewing = _volume != nullptr && _volume->is_previewing();
    return entries<JarTerrainFeature>(_featuresEnabled && (!previewing || _previewFeatures));
}

std::vector<JarVoxelPopulation::Entry<JarTerrainDetail>> JarVoxelPopulation::detail_entries() const
{
    const bool previewing = _volume != nullptr && _volume->is_previewing();
    return entries<JarTerrainDetail>(_detailsEnabled && (!previewing || _previewDetails));
}

void JarVoxelPopulation::remove_features(const Vector3 &global_position, float radius)
{
    if (!_features || _volume == nullptr)
        return;
    const Vector3 local = _volume->to_local(global_position);
    const glm::vec3 c(local.x, local.y, local.z);
    _features->remove_in(Bounds(c - glm::vec3(radius), c + glm::vec3(radius)));
}

TypedArray<Dictionary> JarVoxelPopulation::get_features_near(const Vector3 &global_position, float radius) const
{
    TypedArray<Dictionary> result;
    if (!_features || _volume == nullptr)
        return result;
    const Vector3 local = _volume->to_local(global_position);
    for (const FeatureManager::Instance &instance :
         _features->instances_near(glm::vec3(local.x, local.y, local.z), radius))
    {
        Dictionary d;
        d["feature"] = _features->feature_at(instance.feature);
        d["transform"] = _volume->get_global_transform() * instance.transform;
        result.push_back(d);
    }
    return result;
}

void JarVoxelPopulation::process(JarVoxelVolume &volume, double delta)
{
    if (!ensure_running())
        return;
    {
        const VoxelStats::Scope features(VoxelStats::MainFeatures);
        _features->process(); // independent of the octree: runs during builds too
    }
    const VoxelStats::Scope details(VoxelStats::MainDetails);
    _details->process(); // likewise (only touches chunks)
}

void JarVoxelPopulation::edit_queued(JarVoxelVolume &volume, const ModifySettings &edit, float reach)
{
    if (_features)
        _features->remove_edited(edit, reach);
}

void JarVoxelPopulation::chunk_meshed(JarVoxelVolume &volume, JarVoxelChunk *chunk)
{
    if (ensure_running())
        _details->track(chunk);
}

void JarVoxelPopulation::chunk_gone(JarVoxelVolume &volume, JarVoxelChunk *chunk)
{
    if (_details)
        _details->untrack(chunk);
}

void JarVoxelPopulation::population_invalidated(JarVoxelVolume &volume)
{
    regenerate();
}

void JarVoxelPopulation::volume_cleared(JarVoxelVolume &volume)
{
    stop();
}

bool JarVoxelPopulation::is_generating(const JarVoxelVolume &volume) const
{
    return _details && _details->busy();
}

void JarVoxelPopulation::_bind_methods()
{
#define BIND_POPULATION(m_name, m_type, m_hint, m_hint_text)                                                           \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarVoxelPopulation::get_##m_name);                                 \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarVoxelPopulation::set_##m_name);                        \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
    BIND_POPULATION(table, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "JarPopulationTable");
    BIND_POPULATION(features_enabled, Variant::BOOL, PROPERTY_HINT_NONE, "");
    BIND_POPULATION(details_enabled, Variant::BOOL, PROPERTY_HINT_NONE, "");
    BIND_POPULATION(feature_cell_size, Variant::FLOAT, PROPERTY_HINT_RANGE, "8,512,1,or_greater,suffix:m");
    ADD_GROUP("Editor Preview", "");
    BIND_POPULATION(preview_features, Variant::BOOL, PROPERTY_HINT_NONE, "");
    BIND_POPULATION(preview_details, Variant::BOOL, PROPERTY_HINT_NONE, "");
#undef BIND_POPULATION
    ClassDB::bind_method(D_METHOD("regenerate"), &JarVoxelPopulation::regenerate);
    ClassDB::bind_method(D_METHOD("remove_features", "global_position", "radius"), &JarVoxelPopulation::remove_features);
    ClassDB::bind_method(D_METHOD("get_features_near", "global_position", "radius"),
                         &JarVoxelPopulation::get_features_near);
}
