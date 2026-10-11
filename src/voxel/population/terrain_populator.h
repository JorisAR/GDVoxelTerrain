#ifndef JAR_TERRAIN_POPULATOR_H
#define JAR_TERRAIN_POPULATOR_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/transform3d.hpp>

using namespace godot;

// Small deterministic random generator (PCG-style hash), so population is the
// same every time a region is generated, on any thread.
struct PopulationRandom
{
    uint32_t state;
    explicit PopulationRandom(uint32_t seed) : state(seed * 747796405u + 2891336453u) {}
    uint32_t next_u32()
    {
        state = state * 747796405u + 2891336453u;
        uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }
    float next() { return float(next_u32() >> 8) * (1.0f / 16777216.0f); } // [0, 1)
    static uint32_t hash(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0)
    {
        uint32_t h = a * 0x8da6b343u ^ b * 0xd8163841u ^ c * 0xcb1ab31fu ^ d * 0x165667b1u;
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        return h ^ (h >> 16);
    }
};

// Placement rules shared by details (small, dense, close by) and features
// (large, persistent, seen from afar). Read from worker threads while
// populating, so treat these resources as read-only while the game runs.
class JarTerrainPopulator : public Resource
{
    GDCLASS(JarTerrainPopulator, Resource);

  private:
    float minimum_scale = 1.0f;
    float maximum_scale = 1.0f;
    float minimum_height = -100000.0f;
    float maximum_height = 100000.0f;
    float maximum_slope = 30.0f; // degrees between the surface normal and up
    bool align_with_normal = false;
    bool random_rotation = true;
    float sink = 0.0f; // meters pushed into the ground
    float height_falloff = 0.0f; // meters over which the height limits fade in
    float slope_falloff = 5.0f;  // degrees over which the slope limit fades in
    Ref<FastNoiseLite> mask_noise;
    float mask_threshold = 0.0f;
    float mask_falloff = 0.1f; // noise range over which the mask fades in
    bool mask_invert = false;
    float cluster_spacing = 0.0f; // meters between cluster centers; 0 = no clusters
    float cluster_radius = 10.0f;

    static float ramp(float x) // 0 below 0, 1 above 1, smooth between
    {
        const float t = std::clamp(x, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

  public:
    float get_minimum_scale() const { return minimum_scale; }
    void set_minimum_scale(float value) { minimum_scale = value; }
    float get_maximum_scale() const { return maximum_scale; }
    void set_maximum_scale(float value) { maximum_scale = value; }
    float get_minimum_height() const { return minimum_height; }
    void set_minimum_height(float value) { minimum_height = value; }
    float get_maximum_height() const { return maximum_height; }
    void set_maximum_height(float value) { maximum_height = value; }
    float get_maximum_slope() const { return maximum_slope; }
    void set_maximum_slope(float value) { maximum_slope = std::clamp(value, 0.0f, 180.0f); }
    bool get_align_with_normal() const { return align_with_normal; }
    void set_align_with_normal(bool value) { align_with_normal = value; }
    bool get_random_rotation() const { return random_rotation; }
    void set_random_rotation(bool value) { random_rotation = value; }
    float get_sink() const { return sink; }
    void set_sink(float value) { sink = value; }
    Ref<FastNoiseLite> get_mask_noise() const { return mask_noise; }
    void set_mask_noise(const Ref<FastNoiseLite> &value)
    {
        mask_noise = value;
        notify_property_list_changed();
    }
    float get_mask_threshold() const { return mask_threshold; }
    void set_mask_threshold(float value) { mask_threshold = value; }
    float get_mask_falloff() const { return mask_falloff; }
    void set_mask_falloff(float value) { mask_falloff = std::max(0.0f, value); }
    bool get_mask_invert() const { return mask_invert; }
    void set_mask_invert(bool value) { mask_invert = value; }
    float get_height_falloff() const { return height_falloff; }
    void set_height_falloff(float value) { height_falloff = std::max(0.0f, value); }
    float get_slope_falloff() const { return slope_falloff; }
    void set_slope_falloff(float value) { slope_falloff = std::max(0.0f, value); }
    float get_cluster_spacing() const { return cluster_spacing; }
    void set_cluster_spacing(float value)
    {
        cluster_spacing = std::max(0.0f, value);
        notify_property_list_changed();
    }
    float get_cluster_radius() const { return cluster_radius; }
    void set_cluster_radius(float value) { cluster_radius = std::max(0.01f, value); }

    // Probability (0..1) from the position alone: mask and clusters. Cheap, so
    // placement tests it before finding the exact surface. The density settings
    // are the density where this is 1 (inside forests, in cluster centers).
    float distribution(const glm::vec3 &position, const glm::vec3 &up) const
    {
        float p = 1.0f;
        if (mask_noise.is_valid())
        {
            float n = mask_noise->get_noise_3d(position.x, position.y, position.z);
            if (mask_invert)
                n = -n;
            p *= mask_falloff > 0.0f ? ramp((n - mask_threshold) / mask_falloff + 0.5f) : (n >= mask_threshold ? 1.0f : 0.0f);
        }
        if (cluster_spacing > 0.0f && p > 0.0f)
        {
            // Distance along the ground to the nearest cluster center: one
            // random point per grid cell of cluster_spacing (Worley noise).
            // Centers more than half a cell above or below are skipped, so the
            // ground sees about one center per cell of area, flat or round.
            const glm::vec3 g = position / cluster_spacing;
            const glm::ivec3 base = glm::ivec3(glm::floor(g));
            float nearest = 1e9f;
            for (int z = -1; z <= 1; ++z)
                for (int y = -1; y <= 1; ++y)
                    for (int x = -1; x <= 1; ++x)
                    {
                        const glm::ivec3 c = base + glm::ivec3(x, y, z);
                        PopulationRandom r(PopulationRandom::hash(uint32_t(c.x), uint32_t(c.y), uint32_t(c.z), 0xc1u));
                        const glm::vec3 delta = glm::vec3(c) + glm::vec3(r.next(), r.next(), r.next()) - g;
                        const float along = glm::dot(delta, up);
                        if (std::abs(along) > 0.5f)
                            continue;
                        nearest = std::min(nearest, glm::length(delta - up * along));
                    }
            const float d = nearest * cluster_spacing / cluster_radius;
            p *= std::exp(-d * d);
        }
        return p;
    }

    // Probability (0..1) from the ground: height above sea level and slope,
    // fading in over the falloffs (inside the limits).
    float terrain_fit(float height, const glm::vec3 &normal, const glm::vec3 &up) const
    {
        float p = 1.0f;
        if (height_falloff > 0.0f)
            p *= ramp((height - minimum_height) / height_falloff) * ramp((maximum_height - height) / height_falloff);
        else if (height < minimum_height || height > maximum_height)
            return 0.0f;
        const float slope = std::acos(std::clamp(glm::dot(normal, up), -1.0f, 1.0f)) * 180.0f / float(Math_PI);
        if (slope_falloff > 0.0f)
            p *= ramp((maximum_slope - slope) / slope_falloff);
        else if (slope > maximum_slope)
            return 0.0f;
        return p;
    }

    float probability(const glm::vec3 &position, float height, const glm::vec3 &normal, const glm::vec3 &up) const
    {
        return terrain_fit(height, normal, up) * distribution(position, up);
    }

    // Volume-space transform for an instance at `position`. `up` is the volume's
    // up there; `normal` the surface normal. r1, r2 are random numbers in [0, 1).
    Transform3D build_transform(const glm::vec3 &position, const glm::vec3 &up, const glm::vec3 &normal, float r1,
                                float r2) const
    {
        const glm::vec3 y = glm::normalize(align_with_normal ? normal : up);
        glm::vec3 x = glm::cross(y, std::abs(y.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1));
        x = glm::normalize(x);
        glm::vec3 z = glm::cross(x, y);
        if (random_rotation)
        {
            const float a = r1 * float(Math_TAU);
            const glm::vec3 rx = x * std::cos(a) - z * std::sin(a);
            z = glm::cross(rx, y);
            x = rx;
        }
        const float s = minimum_scale + (maximum_scale - minimum_scale) * r2;
        const glm::vec3 origin = position - up * sink;
        Basis basis(Vector3(x.x, y.x, z.x) * s, Vector3(x.y, y.y, z.y) * s, Vector3(x.z, y.z, z.z) * s);
        return Transform3D(basis, Vector3(origin.x, origin.y, origin.z));
    }

  protected:
    void _validate_property(PropertyInfo &property) const
    {
        const StringName name = property.name;
        if (mask_noise.is_null() && (name == StringName("mask_threshold") || name == StringName("mask_falloff") ||
                                     name == StringName("mask_invert")))
            property.usage = PROPERTY_USAGE_NO_EDITOR;
        if (cluster_spacing <= 0.0f && name == StringName("cluster_radius"))
            property.usage = PROPERTY_USAGE_NO_EDITOR;
    }

    static void _bind_methods()
    {
#define BIND_POP(m_name, m_type, m_hint, m_hint_text)                                                                  \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarTerrainPopulator::get_##m_name);                                \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarTerrainPopulator::set_##m_name);                       \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
        ADD_GROUP("Placement", "");
        BIND_POP(minimum_scale, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater");
        BIND_POP(maximum_scale, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.01,10,0.01,or_greater");
        BIND_POP(align_with_normal, Variant::BOOL, PROPERTY_HINT_NONE, "");
        BIND_POP(random_rotation, Variant::BOOL, PROPERTY_HINT_NONE, "");
        BIND_POP(sink, Variant::FLOAT, PROPERTY_HINT_RANGE, "-10,10,0.01,or_greater,or_less,suffix:m");
        ADD_GROUP("Rules", "");
        BIND_POP(minimum_height, Variant::FLOAT, PROPERTY_HINT_NONE, "suffix:m");
        BIND_POP(maximum_height, Variant::FLOAT, PROPERTY_HINT_NONE, "suffix:m");
        BIND_POP(height_falloff, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,100,0.1,or_greater,suffix:m");
        BIND_POP(maximum_slope, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,180,0.1,degrees");
        BIND_POP(slope_falloff, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,45,0.1,degrees");
        ADD_GROUP("Distribution", "");
        BIND_POP(mask_noise, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite");
        BIND_POP(mask_threshold, Variant::FLOAT, PROPERTY_HINT_RANGE, "-1,1,0.01");
        BIND_POP(mask_falloff, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
        BIND_POP(mask_invert, Variant::BOOL, PROPERTY_HINT_NONE, "");
        BIND_POP(cluster_spacing, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,500,0.1,or_greater,suffix:m");
        BIND_POP(cluster_radius, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.1,200,0.1,or_greater,suffix:m");
#undef BIND_POP
    }
};

#endif // JAR_TERRAIN_POPULATOR_H
