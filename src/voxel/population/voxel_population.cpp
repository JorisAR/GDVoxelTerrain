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

void JarVoxelPopulation::layers_changed()
{
    if (_regeneratePending)
        return;
    _regeneratePending = true;
    callable_mp(this, &JarVoxelPopulation::regenerate_pending).call_deferred();
}

void JarVoxelPopulation::regenerate_pending()
{
    _regeneratePending = false;
    regenerate();
}

static void find_layers(const Node *node, TypedArray<JarPopulationLayer> &result)
{
    for (int i = 0; i < node->get_child_count(); ++i)
    {
        Node *child = node->get_child(i);
        if (JarPopulationLayer *layer = Object::cast_to<JarPopulationLayer>(child))
            result.push_back(layer);
        else if (Object::cast_to<JarVoxelPopulation>(child) == nullptr)
            find_layers(child, result);
    }
}

TypedArray<JarPopulationLayer> JarVoxelPopulation::get_layers() const
{
    TypedArray<JarPopulationLayer> result;
    find_layers(this, result);
    return result;
}

JarPopulationLayer *JarVoxelPopulation::add_layer(const Ref<Resource> &asset, const Dictionary &biomes)
{
    JarPopulationLayer *layer = nullptr;
    const Ref<JarTerrainFeature> feature = asset;
    const Ref<JarTerrainDetail> detail = asset;
    if (feature.is_valid())
    {
        JarFeatureLayer *made = memnew(JarFeatureLayer);
        made->set_feature(feature);
        layer = made;
    }
    else if (detail.is_valid())
    {
        JarDetailLayer *made = memnew(JarDetailLayer);
        made->set_detail(detail);
        layer = made;
    }
    ERR_FAIL_NULL_V_MSG(layer, nullptr, "A layer grows a JarTerrainFeature or a JarTerrainDetail.");
    if (!biomes.is_empty())
    {
        layer->set_everywhere(false);
        const Array names = biomes.keys();
        for (int64_t i = 0; i < names.size(); ++i)
            layer->set_biome(names[i], biomes[names[i]]);
    }
    add_child(layer);
    return layer;
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
    if (!enabled || _volume == nullptr)
        return result;
    const Ref<JarBiomeMap> map = _volume->get_biome_map();
    const TypedArray<JarBiome> biomes = map.is_valid() ? map->get_biomes() : TypedArray<JarBiome>();
    const TypedArray<JarPopulationLayer> layers = get_layers();
    for (int64_t l = 0; l < layers.size(); ++l)
    {
        const JarPopulationLayer *layer = Object::cast_to<JarPopulationLayer>(layers[l]);
        const Ref<T> item = layer != nullptr ? layer->get_asset() : Ref<Resource>();
        if (item.is_null() || !layer->get_enabled())
            continue;
        if (layer->get_everywhere())
        {
            result.push_back({item, {-1, 1.0f}});
            continue;
        }
        for (int64_t b = 0; b < biomes.size(); ++b)
        {
            const Ref<JarBiome> biome = biomes[b];
            const float density = biome.is_valid() ? layer->get_biome(biome->get_name()) : 0.0f;
            if (density > 0.0f)
                result.push_back({item, {int(b), density}});
        }
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
    BIND_POPULATION(features_enabled, Variant::BOOL, PROPERTY_HINT_NONE, "");
    BIND_POPULATION(details_enabled, Variant::BOOL, PROPERTY_HINT_NONE, "");
    BIND_POPULATION(feature_cell_size, Variant::FLOAT, PROPERTY_HINT_RANGE, "8,512,1,or_greater,suffix:m");
    ADD_GROUP("Editor Preview", "");
    BIND_POPULATION(preview_features, Variant::BOOL, PROPERTY_HINT_NONE, "");
    BIND_POPULATION(preview_details, Variant::BOOL, PROPERTY_HINT_NONE, "");
#undef BIND_POPULATION
    ClassDB::bind_method(D_METHOD("regenerate"), &JarVoxelPopulation::regenerate);
    ClassDB::bind_method(D_METHOD("get_layers"), &JarVoxelPopulation::get_layers);
    ClassDB::bind_method(D_METHOD("add_layer", "asset", "biomes"), &JarVoxelPopulation::add_layer, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("remove_features", "global_position", "radius"), &JarVoxelPopulation::remove_features);
    ClassDB::bind_method(D_METHOD("get_features_near", "global_position", "radius"),
                         &JarVoxelPopulation::get_features_near);
}
