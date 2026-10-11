#ifndef ERODED_TERRAIN_SDF_H
#define ERODED_TERRAIN_SDF_H

#include "signed_distance_field.h"
#include <godot_cpp/classes/fast_noise_lite.hpp>

// Terrain (flat heightfield or planet) with an erosion filter: branching gullies
// and sharp ridges, evaluated per point (no simulation), so it works with
// chunked, infinite and editable terrain.
//
// The erosion technique is by Rune Skovbo Johansen (runevision), building on
// work by Clay John and Felix Westin (Fewes):
//   https://blog.runevision.com/2026/03/fast-and-gorgeous-erosion-filter.html
// This is an independent C++ implementation written from that article (the
// fade approach with stacked fading, normalized gullies and straight gullies),
// extended to 3D stripe cells so it also works on spheres.
class JarErodedTerrainSdf : public JarSignedDistanceField
{
    GDCLASS(JarErodedTerrainSdf, JarSignedDistanceField);

  public:
    enum Shape
    {
        SHAPE_FLAT = 0,   // heightfield along +Y
        SHAPE_SPHERE = 1, // planet of `radius` around the origin
    };

  private:
    int _shape = SHAPE_FLAT;
    float _radius = 1000.0f;
    Ref<FastNoiseLite> _noise;
    float _height = 100.0f;

    // Erosion
    float _erosionStrength = 0.5f;  // slope the gullies add (rise over run)
    float _erosionScale = 80.0f;    // width of the largest gullies, in meters
    int _octaves = 5;
    float _lacunarity = 2.0f;
    float _gain = 0.5f;
    float _detail = 1.5f;      // how far smaller gullies reach onto flatter ground
    float _gullyWeight = 0.5f; // < 1 keeps peaks pointy (strength is compensated)
    float _assumedSlope = 0.6f;

    struct Sample
    {
        float height;      // eroded height above the base surface
        glm::vec3 gradient; // tangent gradient of the height
    };

    // Base surface point and up direction for a query position.
    void frame(const glm::vec3 &pos, glm::vec3 &surface, glm::vec3 &up, float &above) const;
    float base_height(const glm::vec3 &surface) const;
    glm::vec3 base_gradient(const glm::vec3 &surface, const glm::vec3 &up, float h, float step) const;
    float max_deviation() const;
    Sample erode(const glm::vec3 &surface, const glm::vec3 &up, float h) const;

  public:
    float distance(const glm::vec3 &pos) const override;
    Bounds bounds() const override;

    // Eroded height (above the base plane / sphere) at a surface point, for scripts.
    float sample_height(const Vector3 &position) const;

    int get_shape() const { return _shape; }
    void set_shape(int v) { set_and_notify(_shape, v); }
    float get_radius() const { return _radius; }
    void set_radius(float v) { set_and_notify(_radius, std::max(1.0f, v)); }
    Ref<FastNoiseLite> get_noise() const { return _noise; }
    void set_noise(const Ref<FastNoiseLite> &v) { set_and_notify(_noise, v); }
    float get_height() const { return _height; }
    void set_height(float v) { set_and_notify(_height, v); }
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
};

VARIANT_ENUM_CAST(JarErodedTerrainSdf::Shape);

#endif // ERODED_TERRAIN_SDF_H
