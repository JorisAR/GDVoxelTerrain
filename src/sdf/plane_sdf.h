#ifndef PLANE_SDF_H
#define PLANE_SDF_H

#include "signed_distance_field.h"

// Infinite half-space: solid below the plane dot(normal, p) + d = 0.
class JarPlaneSdf : public JarSignedDistanceField
{
    GDCLASS(JarPlaneSdf, JarSignedDistanceField);

  private:
    glm::vec3 _normal{0.0f, 1.0f, 0.0f};
    float _d = 0.0f;

  public:
    void set_normal(const Vector3 &normal)
    {
        const glm::vec3 n(normal.x, normal.y, normal.z);
        const float len = glm::length(n);
        set_and_notify(_normal, len > 1.0e-6f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f));
    }
    Vector3 get_normal() const { return Vector3(_normal.x, _normal.y, _normal.z); }
    void set_d(float d) { set_and_notify(_d, d); }
    float get_d() const { return _d; }

    float distance(const glm::vec3 &pos) const override
    {
        return glm::dot(_normal, pos) + _d;
    }

    glm::vec3 normal(const glm::vec3 &) const override
    {
        return _normal;
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_normal", "normal"), &JarPlaneSdf::set_normal);
        ClassDB::bind_method(D_METHOD("get_normal"), &JarPlaneSdf::get_normal);
        ClassDB::bind_method(D_METHOD("set_d", "d"), &JarPlaneSdf::set_d);
        ClassDB::bind_method(D_METHOD("get_d"), &JarPlaneSdf::get_d);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "normal"), "set_normal", "get_normal");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "d"), "set_d", "get_d");
    }
};

#endif // PLANE_SDF_H
