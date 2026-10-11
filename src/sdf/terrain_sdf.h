#ifndef TERRAIN_SDF_H
#define TERRAIN_SDF_H

#include "signed_distance_field.h"
#include <godot_cpp/classes/fast_noise_lite.hpp>

// Terrain generator: a flat heightfield or a planet, built from layers that are
// all evaluated per point (so it works with chunked, infinite, editable
// volumes):
//   1. base noise: rolling hills above sea level, scaled separately below it
//      (`depth`) for ocean floors;
//   2. mountains: a low-frequency mask raises mountain ranges where it passes
//      a threshold;
//   3. erosion: branching gullies and sharp ridges on top of both.
//
// The erosion filter is by Rune Skovbo Johansen (runevision), building on work
// by Clay John and Felix Westin (Fewes):
//   https://blog.runevision.com/2026/03/fast-and-gorgeous-erosion-filter.html
// This is an independent C++ implementation written from that article (the
// fade approach with stacked fading, normalized gullies and straight gullies),
// extended to 3D stripe cells so it also works on spheres.
class JarTerrainSdf : public JarSignedDistanceField
{
    GDCLASS(JarTerrainSdf, JarSignedDistanceField);

  public:
    enum Shape
    {
        SHAPE_FLAT = 0,   // heightfield along +Y
        SHAPE_SPHERE = 1, // planet of `radius` around the origin
    };

  private:
    int _shape = SHAPE_FLAT;
    float _radius = 1000.0f;
    float _seaLevel = 0.0f; // meters above the base plane / sphere

    Ref<FastNoiseLite> _noise;
    float _height = 60.0f; // amplitude of positive noise
    float _depth = 60.0f;  // amplitude of negative noise

    Ref<FastNoiseLite> _mountainNoise;
    float _mountainHeight = 200.0f;
    float _mountainThreshold = 0.2f; // mask value where mountains start
    float _mountainBlend = 0.5f;     // mask range over which they rise to full height

    bool _erosion = true;
    float _erosionStrength = 0.5f; // slope the gullies add (rise over run)
    float _erosionScale = 80.0f;   // width of the largest gullies, in meters
    int _octaves = 4;
    float _lacunarity = 2.0f;
    float _gain = 0.5f;
    float _detail = 1.5f;      // how far smaller gullies reach onto flatter ground
    float _gullyWeight = 0.5f; // < 1 keeps peaks pointy (strength is compensated)
    float _assumedSlope = 0.6f;

    struct Sample
    {
        float height;       // eroded height above the base surface
        glm::vec3 gradient; // tangent gradient of the height (see evaluate)
        float layered;      // height before erosion
    };

    void frame(const glm::vec3 &pos, glm::vec3 &surface, glm::vec3 &up, float &above) const;
    float layered_height(const glm::vec3 &surface) const; // base noise + mountains
    float max_layered_height() const;
    glm::vec3 height_gradient(const glm::vec3 &surface, const glm::vec3 &up, float h, float step) const;
    float max_deviation() const;
    // Without `localGradient` the gradient only holds the erosion's part (the
    // layers' slope costs two more noise samples); add it with height_gradient.
    Sample evaluate(const glm::vec3 &surface, const glm::vec3 &up, bool localGradient = true) const;

    template <typename T> void set_shape_property(T &field, const T &value)
    {
        set_and_notify(field, value);
        notify_property_list_changed();
    }

  public:
    float distance(const glm::vec3 &pos) const override;
    Bounds bounds() const override;
    bool natural_up(UpField &out) const override;

    // Terrain height (above the base plane / sphere) at a position, for scripts.
    float sample_height(const Vector3 &position) const;

    int get_shape() const { return _shape; }
    void set_shape(int v) { set_shape_property(_shape, v); }
    float get_radius() const { return _radius; }
    void set_radius(float v) { set_and_notify(_radius, std::max(1.0f, v)); }
    float get_sea_level() const { return _seaLevel; }
    void set_sea_level(float v) { set_and_notify(_seaLevel, v); }
    Ref<FastNoiseLite> get_noise() const { return _noise; }
    void set_noise(const Ref<FastNoiseLite> &v) { set_and_notify(_noise, v); }
    float get_height() const { return _height; }
    void set_height(float v) { set_and_notify(_height, v); }
    float get_depth() const { return _depth; }
    void set_depth(float v) { set_and_notify(_depth, v); }
    Ref<FastNoiseLite> get_mountain_noise() const { return _mountainNoise; }
    void set_mountain_noise(const Ref<FastNoiseLite> &v) { set_shape_property(_mountainNoise, v); }
    float get_mountain_height() const { return _mountainHeight; }
    void set_mountain_height(float v) { set_and_notify(_mountainHeight, std::max(0.0f, v)); }
    float get_mountain_threshold() const { return _mountainThreshold; }
    void set_mountain_threshold(float v) { set_and_notify(_mountainThreshold, v); }
    float get_mountain_blend() const { return _mountainBlend; }
    void set_mountain_blend(float v) { set_and_notify(_mountainBlend, std::max(0.01f, v)); }
    bool get_erosion() const { return _erosion; }
    void set_erosion(bool v) { set_shape_property(_erosion, v); }
    float get_erosion_strength() const { return _erosionStrength; }
    void set_erosion_strength(float v) { set_and_notify(_erosionStrength, std::max(0.0f, v)); }
    float get_erosion_scale() const { return _erosionScale; }
    void set_erosion_scale(float v) { set_and_notify(_erosionScale, std::max(0.01f, v)); }
    int get_octaves() const { return _octaves; }
    void set_octaves(int v) { set_and_notify(_octaves, std::clamp(v, 0, 10)); }
    float get_lacunarity() const { return _lacunarity; }
    void set_lacunarity(float v) { set_and_notify(_lacunarity, std::max(1.0f, v)); }
    float get_gain() const { return _gain; }
    void set_gain(float v) { set_and_notify(_gain, std::clamp(v, 0.0f, 1.0f)); }
    float get_detail() const { return _detail; }
    void set_detail(float v) { set_and_notify(_detail, std::max(0.01f, v)); }
    float get_gully_weight() const { return _gullyWeight; }
    void set_gully_weight(float v) { set_and_notify(_gullyWeight, std::clamp(v, 0.01f, 1.0f)); }
    float get_assumed_slope() const { return _assumedSlope; }
    void set_assumed_slope(float v) { set_and_notify(_assumedSlope, std::max(0.01f, v)); }

  protected:
    static void _bind_methods();
    void _validate_property(PropertyInfo &property) const;
};

VARIANT_ENUM_CAST(JarTerrainSdf::Shape);

#endif // TERRAIN_SDF_H
