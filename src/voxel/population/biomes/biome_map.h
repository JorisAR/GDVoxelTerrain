#ifndef JAR_BIOME_MAP_H
#define JAR_BIOME_MAP_H

#include "biome.h"
#include "up_field.h"
#include <glm/glm.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace godot;

// Biome weights at a point: the strongest few biomes (indices into the map's
// biomes), weights summing to 1.
struct BiomeWeights
{
    static constexpr int Slots = 4;
    int ids[Slots] = {0, 0, 0, 0};
    float weights[Slots] = {1.0f, 0.0f, 0.0f, 0.0f};
    int count = 1;
    float weight_of(int biome) const
    {
        for (int i = 0; i < count; ++i)
            if (ids[i] == biome)
                return weights[i];
        return 0.0f;
    }
};

// Decides which biome is where, from a simple climate model:
//   temperature = temperature noise - lapse rate * height - latitude effect
//   moisture    = moisture noise
// Each biome sits at a point in (temperature, moisture) space; a position
// belongs to the biomes nearest to its climate, blended smoothly over
// `blend`, so biome borders are soft and follow the climate, not a grid.
// Evaluated per point and thread-safe (read-only), like the terrain itself.
class JarBiomeMap : public Resource
{
    GDCLASS(JarBiomeMap, Resource);

  private:
    TypedArray<JarBiome> biomes;
    Ref<FastNoiseLite> temperature_noise;
    Ref<FastNoiseLite> moisture_noise;
    float temperature_lapse = 0.2f; // temperature drop per 100 m above sea level
    float latitude_effect = 0.6f;   // planets: temperature drop at the poles
    Vector3 pole_axis = Vector3(0, 1, 0);
    float blend = 0.25f; // climate distance over which biomes blend

  public:
    TypedArray<JarBiome> get_biomes() const { return biomes; }
    void set_biomes(const TypedArray<JarBiome> &v) { biomes = v; emit_changed(); }
    Ref<FastNoiseLite> get_temperature_noise() const { return temperature_noise; }
    void set_temperature_noise(const Ref<FastNoiseLite> &v) { temperature_noise = v; emit_changed(); }
    Ref<FastNoiseLite> get_moisture_noise() const { return moisture_noise; }
    void set_moisture_noise(const Ref<FastNoiseLite> &v) { moisture_noise = v; emit_changed(); }
    float get_temperature_lapse() const { return temperature_lapse; }
    void set_temperature_lapse(float v) { temperature_lapse = v; emit_changed(); }
    float get_latitude_effect() const { return latitude_effect; }
    void set_latitude_effect(float v) { latitude_effect = v; emit_changed(); }
    Vector3 get_pole_axis() const { return pole_axis; }
    void set_pole_axis(const Vector3 &v) { pole_axis = v.normalized(); emit_changed(); }
    float get_blend() const { return blend; }
    void set_blend(float v) { blend = std::max(0.01f, v); emit_changed(); }

    // Climate at a volume-space position: (temperature, moisture).
    glm::vec2 climate(const glm::vec3 &position, const UpField &up) const;
    BiomeWeights evaluate(const glm::vec3 &position, const UpField &up) const;
    int biome_count() const { return int(biomes.size()); }

  protected:
    static void _bind_methods();
};

#endif // JAR_BIOME_MAP_H
