#include "population_layer.h"
#include "voxel_population.h"
#include "voxel_volume.h"

JarVoxelPopulation *JarPopulationLayer::population() const
{
    for (Node *node = get_parent(); node != nullptr; node = node->get_parent())
        if (JarVoxelPopulation *found = Object::cast_to<JarVoxelPopulation>(node))
            return found;
    return nullptr;
}

PackedStringArray JarPopulationLayer::biome_names() const
{
    PackedStringArray names;
    const JarVoxelPopulation *owner = population();
    const JarVoxelVolume *volume = owner != nullptr ? Object::cast_to<JarVoxelVolume>(owner->get_parent()) : nullptr;
    const Ref<JarBiomeMap> map = volume != nullptr ? volume->get_biome_map() : Ref<JarBiomeMap>();
    if (map.is_valid())
    {
        const TypedArray<JarBiome> list = map->get_biomes();
        for (int64_t i = 0; i < list.size(); ++i)
        {
            const Ref<JarBiome> biome = list[i];
            if (biome.is_valid() && !biome->get_name().is_empty())
                names.push_back(biome->get_name());
        }
    }
    // Stored ones stay, or a layer outside its volume would lose them on saving.
    const Array stored = biomes.keys();
    for (int64_t i = 0; i < stored.size(); ++i)
        if (!names.has(stored[i]))
            names.push_back(stored[i]);
    return names;
}

void JarPopulationLayer::changed()
{
    if (JarVoxelPopulation *owner = population())
        owner->layers_changed();
    if (is_inside_tree())
        update_configuration_warnings();
}

void JarPopulationLayer::_notification(int what)
{
    if (what == NOTIFICATION_ENTER_TREE || what == NOTIFICATION_EXIT_TREE || what == NOTIFICATION_MOVED_IN_PARENT)
        changed();
}

void JarPopulationLayer::set_enabled(bool value)
{
    if (enabled == value)
        return;
    enabled = value;
    changed();
}

void JarPopulationLayer::set_everywhere(bool value)
{
    if (everywhere == value)
        return;
    everywhere = value;
    notify_property_list_changed();
    changed();
}

float JarPopulationLayer::get_biome(const String &name) const
{
    return biomes.get(name, 0.0f);
}

void JarPopulationLayer::set_biome(const String &name, float density)
{
    if (density > 0.0f)
        biomes[name] = density;
    else
        biomes.erase(name);
    changed();
}

bool JarPopulationLayer::_set(const StringName &name, const Variant &value)
{
    const String path = name;
    if (!path.begins_with(Prefix))
        return false;
    set_biome(path.trim_prefix(Prefix), value);
    return true;
}

bool JarPopulationLayer::_get(const StringName &name, Variant &value) const
{
    const String path = name;
    if (!path.begins_with(Prefix))
        return false;
    value = get_biome(path.trim_prefix(Prefix));
    return true;
}

void JarPopulationLayer::_get_property_list(List<PropertyInfo> *list) const
{
    const PackedStringArray names = biome_names();
    for (int64_t i = 0; i < names.size(); ++i)
        list->push_back(PropertyInfo(Variant::FLOAT, String(Prefix) + names[i], PROPERTY_HINT_RANGE, "0,2,0.01,or_greater",
                                     everywhere ? uint32_t(PROPERTY_USAGE_STORAGE) : uint32_t(PROPERTY_USAGE_DEFAULT)));
}

bool JarPopulationLayer::_property_can_revert(const StringName &name) const
{
    return String(name).begins_with(Prefix);
}

bool JarPopulationLayer::_property_get_revert(const StringName &name, Variant &value) const
{
    if (!String(name).begins_with(Prefix))
        return false;
    value = 0.0f;
    return true;
}

PackedStringArray JarPopulationLayer::_get_configuration_warnings() const
{
    PackedStringArray warnings;
    if (get_asset().is_null())
        warnings.push_back("Nothing to grow: assign its resource.");
    if (population() == nullptr)
        warnings.push_back("Only grows below a JarVoxelPopulation.");
    if (!everywhere)
    {
        bool any = false;
        const PackedStringArray known = biome_names();
        const Array stored = biomes.keys();
        for (int64_t i = 0; i < stored.size(); ++i)
            any = any || known.has(stored[i]);
        if (!any)
            warnings.push_back("Grows in no biome: set a biome's density, or turn on Everywhere.");
    }
    return warnings;
}

void JarPopulationLayer::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_enabled"), &JarPopulationLayer::get_enabled);
    ClassDB::bind_method(D_METHOD("set_enabled", "value"), &JarPopulationLayer::set_enabled);
    ClassDB::bind_method(D_METHOD("get_everywhere"), &JarPopulationLayer::get_everywhere);
    ClassDB::bind_method(D_METHOD("set_everywhere", "value"), &JarPopulationLayer::set_everywhere);
    ClassDB::bind_method(D_METHOD("get_biome", "name"), &JarPopulationLayer::get_biome);
    ClassDB::bind_method(D_METHOD("set_biome", "name", "density"), &JarPopulationLayer::set_biome);
    ClassDB::bind_method(D_METHOD("get_biomes"), &JarPopulationLayer::get_biomes);
    ClassDB::bind_method(D_METHOD("get_asset"), &JarPopulationLayer::get_asset);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "get_enabled");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "everywhere"), "set_everywhere", "get_everywhere");
}

void JarFeatureLayer::set_feature(const Ref<JarTerrainFeature> &value)
{
    feature = value;
    changed();
}

void JarFeatureLayer::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_feature"), &JarFeatureLayer::get_feature);
    ClassDB::bind_method(D_METHOD("set_feature", "value"), &JarFeatureLayer::set_feature);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "feature", PROPERTY_HINT_RESOURCE_TYPE, "JarTerrainFeature"), "set_feature",
                 "get_feature");
}

void JarDetailLayer::set_detail(const Ref<JarTerrainDetail> &value)
{
    detail = value;
    changed();
}

void JarDetailLayer::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_detail"), &JarDetailLayer::get_detail);
    ClassDB::bind_method(D_METHOD("set_detail", "value"), &JarDetailLayer::set_detail);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "detail", PROPERTY_HINT_RESOURCE_TYPE, "JarTerrainDetail"), "set_detail",
                 "get_detail");
}
