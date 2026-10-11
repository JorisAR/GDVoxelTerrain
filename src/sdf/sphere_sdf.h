#ifndef SPHERE_SDF_H
#define SPHERE_SDF_H

#include "signed_distance_field.h"

class JarSphereSdf : public JarSignedDistanceField
{
    GDCLASS(JarSphereSdf, JarSignedDistanceField);

  private:
    glm::vec3 _center{0.0f};
    float _radius = 1.0f;

  public:
    void set_center(const Vector3 &c) { set_and_notify(_center, glm::vec3(c.x, c.y, c.z)); }
    Vector3 get_center() const { return Vector3(_center.x, _center.y, _center.z); }
    void set_radius(float r) { set_and_notify(_radius, r); }
    float get_radius() const { return _radius; }

    float distance(const glm::vec3 &pos) const override
    {
        return glm::length(pos - _center) - _radius;
    }

    glm::vec3 normal(const glm::vec3 &pos) const override
    {
        const glm::vec3 d = pos - _center;
        const float len = glm::length(d);
        return len > 0.0f ? d / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    Bounds bounds() const override
    {
        return Bounds(_center - glm::vec3(_radius), _center + glm::vec3(_radius));
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_center", "center"), &JarSphereSdf::set_center);
        ClassDB::bind_method(D_METHOD("get_center"), &JarSphereSdf::get_center);
        ClassDB::bind_method(D_METHOD("set_radius", "radius"), &JarSphereSdf::set_radius);
        ClassDB::bind_method(D_METHOD("get_radius"), &JarSphereSdf::get_radius);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "center"), "set_center", "get_center");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,1024,0.01,or_greater"),
                     "set_radius", "get_radius");
    }
};

#endif // SPHERE_SDF_H
