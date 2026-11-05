#ifndef JAR_POPULATION_LAYER_H
#define JAR_POPULATION_LAYER_H

#include "terrain_detail.h"
#include "terrain_feature.h"
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace godot;

class JarVoxelPopulation;

// One thing that grows, as a node below a JarVoxelPopulation: an asset (the
// rules are in its resource, shared between layers and planets) and where it
// grows. Tree order is layer order: earlier features claim their footprint
// first. Plain nodes between a population and its layers work as folders.
//
// `everywhere` grows it regardless of biomes; otherwise each biome of the
// volume's biome map has a density multiplier (0: it does not grow there),
// shown and saved as "biomes/<name>" properties.
class JarPopulationLayer : public Node
{
    GDCLASS(JarPopulationLayer, Node);

  private:
    bool enabled = true;
    bool everywhere = true;
    Dictionary biomes; // biome name -> density multiplier, without zeros

    static constexpr const char *Prefix = "biomes/";
    JarVoxelPopulation *population() const;
    PackedStringArray biome_names() const; // of the volume's map, then any others stored

  protected:
    void changed();
    static void _bind_methods();
    void _notification(int what);
    bool _set(const StringName &name, const Variant &value);
    bool _get(const StringName &name, Variant &value) const;
    void _get_property_list(List<PropertyInfo> *list) const;
    bool _property_can_revert(const StringName &name) const;
    bool _property_get_revert(const StringName &name, Variant &value) const;

  public:
    virtual Ref<Resource> get_asset() const { return Ref<Resource>(); }
    bool get_enabled() const { return enabled; }
    void set_enabled(bool value);
    bool get_everywhere() const { return everywhere; }
    void set_everywhere(bool value);
    float get_biome(const String &name) const;
    void set_biome(const String &name, float density);
    Dictionary get_biomes() const { return biomes; }
    PackedStringArray _get_configuration_warnings() const override;
};

class JarFeatureLayer : public JarPopulationLayer
{
    GDCLASS(JarFeatureLayer, JarPopulationLayer);

  private:
    Ref<JarTerrainFeature> feature;

  protected:
    static void _bind_methods();

  public:
    Ref<Resource> get_asset() const override { return feature; }
    Ref<JarTerrainFeature> get_feature() const { return feature; }
    void set_feature(const Ref<JarTerrainFeature> &value);
};

class JarDetailLayer : public JarPopulationLayer
{
    GDCLASS(JarDetailLayer, JarPopulationLayer);

  private:
    Ref<JarTerrainDetail> detail;

  protected:
    static void _bind_methods();

  public:
    Ref<Resource> get_asset() const override { return detail; }
    Ref<JarTerrainDetail> get_detail() const { return detail; }
    void set_detail(const Ref<JarTerrainDetail> &value);
};

#endif // JAR_POPULATION_LAYER_H
