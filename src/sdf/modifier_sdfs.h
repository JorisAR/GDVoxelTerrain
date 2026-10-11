#ifndef MODIFIER_SDFS_H
#define MODIFIER_SDFS_H

#include "signed_distance_field.h"
#include <godot_cpp/classes/fast_noise_lite.hpp>

// Base for SDFs that wrap a single child field.
class JarModifierSdf : public JarSignedDistanceField
{
    GDCLASS(JarModifierSdf, JarSignedDistanceField);

  protected:
    Ref<JarSignedDistanceField> _sdf;

    float child_distance(const glm::vec3 &pos) const
    {
        return _sdf.is_valid() ? _sdf->distance(pos) : 1e9f;
    }
    Bounds child_bounds() const
    {
        return _sdf.is_valid() ? _sdf->bounds() : Bounds();
    }

    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_sdf", "sdf"), &JarModifierSdf::set_sdf);
        ClassDB::bind_method(D_METHOD("get_sdf"), &JarModifierSdf::get_sdf);
        ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sdf", PROPERTY_HINT_RESOURCE_TYPE, "JarSignedDistanceField"),
                     "set_sdf", "get_sdf");
    }

  public:
    void set_sdf(const Ref<JarSignedDistanceField> &s) { set_and_notify(_sdf, s); }
    Ref<JarSignedDistanceField> get_sdf() const { return _sdf; }

    float distance(const glm::vec3 &pos) const override { return child_distance(pos); }
    Bounds bounds() const override { return child_bounds(); }
    void prepare() override
    {
        if (_sdf.is_valid())
            _sdf->prepare();
    }
};

// Grows (offset > 0) or shrinks the child surface, rounding its edges, and can
// hollow it into a shell of the given thickness.
class JarOffsetSdf : public JarModifierSdf
{
    GDCLASS(JarOffsetSdf, JarModifierSdf);

  private:
    float _offset = 0.0f;
    float _shellThickness = 0.0f; // > 0: keep only a shell ("onion")

  public:
    void set_offset(float v) { set_and_notify(_offset, v); }
    float get_offset() const { return _offset; }
    void set_shell_thickness(float v) { set_and_notify(_shellThickness, std::max(0.0f, v)); }
    float get_shell_thickness() const { return _shellThickness; }

    float distance(const glm::vec3 &pos) const override
    {
        float d = child_distance(pos) - _offset;
        if (_shellThickness > 0.0f)
            d = std::abs(d) - _shellThickness * 0.5f;
        return d;
    }

    Bounds bounds() const override
    {
        return child_bounds().expanded(std::max(0.0f, _offset) + _shellThickness * 0.5f);
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_offset", "offset"), &JarOffsetSdf::set_offset);
        ClassDB::bind_method(D_METHOD("get_offset"), &JarOffsetSdf::get_offset);
        ClassDB::bind_method(D_METHOD("set_shell_thickness", "thickness"), &JarOffsetSdf::set_shell_thickness);
        ClassDB::bind_method(D_METHOD("get_shell_thickness"), &JarOffsetSdf::get_shell_thickness);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "offset"), "set_offset", "get_offset");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shell_thickness", PROPERTY_HINT_RANGE, "0,64,0.01,or_greater"),
                     "set_shell_thickness", "get_shell_thickness");
    }
};

// Repeats the child on a grid (pillars, fences, crystal fields). A count of 0
// on an axis repeats forever along it; otherwise copies are centered on the
// origin. The child should fit within one cell for exact distances.
class JarRepeatSdf : public JarModifierSdf
{
    GDCLASS(JarRepeatSdf, JarModifierSdf);

  private:
    glm::vec3 _spacing{4.0f};
    glm::ivec3 _count{3, 1, 3};

    glm::vec3 fold(const glm::vec3 &pos) const
    {
        glm::vec3 q = pos;
        for (int i = 0; i < 3; ++i)
        {
            const float s = _spacing[i];
            if (s <= 0.0f)
                continue;
            if (_count[i] <= 0)
                q[i] = pos[i] - s * std::round(pos[i] / s);
            else
            {
                // Copies sit at (k - (n-1)/2) * s for k in [0, n).
                const float half = (_count[i] - 1) * 0.5f;
                const float k = std::clamp(std::round(pos[i] / s + half), 0.0f, float(_count[i] - 1));
                q[i] = pos[i] - (k - half) * s;
            }
        }
        return q;
    }

  public:
    void set_spacing(const Vector3 &v) { set_and_notify(_spacing, glm::vec3(v.x, v.y, v.z)); }
    Vector3 get_spacing() const { return Vector3(_spacing.x, _spacing.y, _spacing.z); }
    void set_count(const Vector3i &v) { set_and_notify(_count, glm::max(glm::ivec3(v.x, v.y, v.z), glm::ivec3(0))); }
    Vector3i get_count() const { return Vector3i(_count.x, _count.y, _count.z); }

    float distance(const glm::vec3 &pos) const override
    {
        return child_distance(fold(pos));
    }

    Bounds bounds() const override
    {
        Bounds b = child_bounds();
        if (!b.is_valid())
            return b;
        for (int i = 0; i < 3; ++i)
        {
            if (_count[i] <= 0 && _spacing[i] > 0.0f)
                return Bounds::infinite();
            const float half = std::max(0, _count[i] - 1) * 0.5f * _spacing[i];
            b.min[i] -= half;
            b.max[i] += half;
        }
        return b;
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_spacing", "spacing"), &JarRepeatSdf::set_spacing);
        ClassDB::bind_method(D_METHOD("get_spacing"), &JarRepeatSdf::get_spacing);
        ClassDB::bind_method(D_METHOD("set_count", "count"), &JarRepeatSdf::set_count);
        ClassDB::bind_method(D_METHOD("get_count"), &JarRepeatSdf::get_count);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "spacing"), "set_spacing", "get_spacing");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3I, "count"), "set_count", "get_count");
    }
};

// Displaces the child surface with 3D noise (rocky boulders, eroded caves).
class JarDisplaceSdf : public JarModifierSdf
{
    GDCLASS(JarDisplaceSdf, JarModifierSdf);

  private:
    Ref<FastNoiseLite> _noise;
    float _amplitude = 1.0f;

  public:
    void set_noise(const Ref<FastNoiseLite> &n) { set_and_notify(_noise, n); }
    Ref<FastNoiseLite> get_noise() const { return _noise; }
    void set_amplitude(float a) { set_and_notify(_amplitude, a); }
    float get_amplitude() const { return _amplitude; }

    float distance(const glm::vec3 &pos) const override
    {
        const float d = child_distance(pos);
        // Far from the surface the noise cannot matter; skip the costly sample.
        if (_noise.is_null() || std::abs(d) > std::abs(_amplitude) * 2.0f)
            return d;
        return d + _noise->get_noise_3d(pos.x, pos.y, pos.z) * _amplitude;
    }

    Bounds bounds() const override
    {
        return child_bounds().expanded(std::abs(_amplitude));
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_noise", "noise"), &JarDisplaceSdf::set_noise);
        ClassDB::bind_method(D_METHOD("get_noise"), &JarDisplaceSdf::get_noise);
        ClassDB::bind_method(D_METHOD("set_amplitude", "amplitude"), &JarDisplaceSdf::set_amplitude);
        ClassDB::bind_method(D_METHOD("get_amplitude"), &JarDisplaceSdf::get_amplitude);
        ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "noise", PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite"), "set_noise",
                     "get_noise");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "amplitude"), "set_amplitude", "get_amplitude");
    }
};

#endif // MODIFIER_SDFS_H
