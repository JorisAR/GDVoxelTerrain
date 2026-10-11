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
    Ref<FastNoiseLite> mask_noise;
    float mask_threshold = 0.0f;

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

    // Height above sea level, the surface normal and up at a candidate
    // position (volume space). The mask is checked after placement, so density
    // is the density inside the areas the mask lets through.
    bool accepts(const glm::vec3 &position, float height, const glm::vec3 &normal, const glm::vec3 &up) const
    {
        if (height < minimum_height || height > maximum_height)
            return false;
        const float cosSlope = glm::dot(normal, up);
        if (cosSlope < std::cos(maximum_slope * float(Math_PI) / 180.0f))
            return false;
        if (mask_noise.is_valid() &&
            mask_noise->get_noise_3d(position.x, position.y, position.z) < mask_threshold)
            return false;
        return true;
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
        if (property.name == StringName("mask_threshold") && mask_noise.is_null())
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
        BIND_POP(maximum_slope, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,180,0.1,degrees");
        BIND_POP(mask_noise, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite");
        BIND_POP(mask_threshold, Variant::FLOAT, PROPERTY_HINT_RANGE, "-1,1,0.01");
#undef BIND_POP
    }
};

#endif // JAR_TERRAIN_POPULATOR_H
