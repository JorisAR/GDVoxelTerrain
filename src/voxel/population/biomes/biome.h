#ifndef JAR_BIOME_H
#define JAR_BIOME_H

#include "terrain_detail.h"
#include "terrain_feature.h"
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;

// One biome: where it occurs (a point in climate space: temperature and
// moisture, see JarBiomeMap) and what it looks like: its own features and
// details, and the materials its ground rules use (ground, cliff, shore,
// seabed, peaks; -1 keeps the shader's setting).
class JarBiome : public Resource
{
    GDCLASS(JarBiome, Resource);

  private:
    float temperature = 0.0f; // -1 cold .. 1 hot
    float moisture = 0.0f;    // -1 dry .. 1 wet
    TypedArray<JarTerrainFeature> features;
    TypedArray<JarTerrainDetail> details;
    int ground_material = -1;
    int cliff_material = -1;
    int shore_material = -1;
    int seabed_material = -1;
    int peak_material = -1;

  public:
    float get_temperature() const { return temperature; }
    void set_temperature(float v) { temperature = v; emit_changed(); }
    float get_moisture() const { return moisture; }
    void set_moisture(float v) { moisture = v; emit_changed(); }
    TypedArray<JarTerrainFeature> get_features() const { return features; }
    void set_features(const TypedArray<JarTerrainFeature> &v) { features = v; emit_changed(); }
    TypedArray<JarTerrainDetail> get_details() const { return details; }
    void set_details(const TypedArray<JarTerrainDetail> &v) { details = v; emit_changed(); }
    int get_ground_material() const { return ground_material; }
    void set_ground_material(int v) { ground_material = v; emit_changed(); }
    int get_cliff_material() const { return cliff_material; }
    void set_cliff_material(int v) { cliff_material = v; emit_changed(); }
    int get_shore_material() const { return shore_material; }
    void set_shore_material(int v) { shore_material = v; emit_changed(); }
    int get_seabed_material() const { return seabed_material; }
    void set_seabed_material(int v) { seabed_material = v; emit_changed(); }
    int get_peak_material() const { return peak_material; }
    void set_peak_material(int v) { peak_material = v; emit_changed(); }

  protected:
    static void _bind_methods()
    {
#define BIND_BIOME(m_name, m_type, m_hint, m_hint_text)                                                                \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarBiome::get_##m_name);                                           \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarBiome::set_##m_name);                                  \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
        ADD_GROUP("Climate", "");
        BIND_BIOME(temperature, Variant::FLOAT, PROPERTY_HINT_RANGE, "-1,1,0.01");
        BIND_BIOME(moisture, Variant::FLOAT, PROPERTY_HINT_RANGE, "-1,1,0.01");
        ADD_GROUP("Population", "");
        BIND_BIOME(features, Variant::ARRAY, PROPERTY_HINT_TYPE_STRING,
                   String::num(Variant::OBJECT) + "/" + String::num(PROPERTY_HINT_RESOURCE_TYPE) + ":JarTerrainFeature");
        BIND_BIOME(details, Variant::ARRAY, PROPERTY_HINT_TYPE_STRING,
                   String::num(Variant::OBJECT) + "/" + String::num(PROPERTY_HINT_RESOURCE_TYPE) + ":JarTerrainDetail");
        ADD_GROUP("Ground Materials", "");
        BIND_BIOME(ground_material, Variant::INT, PROPERTY_HINT_RANGE, "-1,254,1");
        BIND_BIOME(cliff_material, Variant::INT, PROPERTY_HINT_RANGE, "-1,254,1");
        BIND_BIOME(shore_material, Variant::INT, PROPERTY_HINT_RANGE, "-1,254,1");
        BIND_BIOME(seabed_material, Variant::INT, PROPERTY_HINT_RANGE, "-1,254,1");
        BIND_BIOME(peak_material, Variant::INT, PROPERTY_HINT_RANGE, "-1,254,1");
#undef BIND_BIOME
    }
};

#endif // JAR_BIOME_H
