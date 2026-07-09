#ifndef CAPSULE_SDF_H
#define CAPSULE_SDF_H

#include "signed_distance_field.h"

// A capsule: a cylinder with rounded ends, defined by two endpoints and a radius.
// Great for limbs, pillars, tunnels (subtracted), pipes, etc.
class JarCapsuleSdf : public JarSignedDistanceField
{
    GDCLASS(JarCapsuleSdf, JarSignedDistanceField);

  private:
    glm::vec3 _a{0.0f, -1.0f, 0.0f};
    glm::vec3 _b{0.0f, 1.0f, 0.0f};
    float _radius = 0.5f;

  public:
    void set_point_a(const Vector3 &v) { _a = glm::vec3(v.x, v.y, v.z); }
    Vector3 get_point_a() const { return Vector3(_a.x, _a.y, _a.z); }
    void set_point_b(const Vector3 &v) { _b = glm::vec3(v.x, v.y, v.z); }
    Vector3 get_point_b() const { return Vector3(_b.x, _b.y, _b.z); }
    void set_radius(float r) { _radius = r; }
    float get_radius() const { return _radius; }

    virtual float distance(const glm::vec3 &pos) const override
    {
        glm::vec3 pa = pos - _a;
        glm::vec3 ba = _b - _a;
        float denom = glm::dot(ba, ba);
        float h = denom > 0.0f ? glm::clamp(glm::dot(pa, ba) / denom, 0.0f, 1.0f) : 0.0f;
        return glm::length(pa - ba * h) - _radius;
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_point_a", "a"), &JarCapsuleSdf::set_point_a);
        ClassDB::bind_method(D_METHOD("get_point_a"), &JarCapsuleSdf::get_point_a);
        ClassDB::bind_method(D_METHOD("set_point_b", "b"), &JarCapsuleSdf::set_point_b);
        ClassDB::bind_method(D_METHOD("get_point_b"), &JarCapsuleSdf::get_point_b);
        ClassDB::bind_method(D_METHOD("set_radius", "r"), &JarCapsuleSdf::set_radius);
        ClassDB::bind_method(D_METHOD("get_radius"), &JarCapsuleSdf::get_radius);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "point_a"), "set_point_a", "get_point_a");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "point_b"), "set_point_b", "get_point_b");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius"), "set_radius", "get_radius");
    }
};

#endif // CAPSULE_SDF_H
