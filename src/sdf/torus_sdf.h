#ifndef TORUS_SDF_H
#define TORUS_SDF_H

#include "signed_distance_field.h"

// A torus (donut/ring) lying in the XZ plane, around the Y axis.
//   major_radius = distance from centre to the middle of the tube
//   minor_radius = tube thickness
class JarTorusSdf : public JarSignedDistanceField
{
    GDCLASS(JarTorusSdf, JarSignedDistanceField);

  private:
    glm::vec3 _center{0.0f, 0.0f, 0.0f};
    float _majorRadius = 2.0f;
    float _minorRadius = 0.5f;

  public:
    void set_center(const Vector3 &c) { _center = glm::vec3(c.x, c.y, c.z); }
    Vector3 get_center() const { return Vector3(_center.x, _center.y, _center.z); }
    void set_major_radius(float r) { _majorRadius = r; }
    float get_major_radius() const { return _majorRadius; }
    void set_minor_radius(float r) { _minorRadius = r; }
    float get_minor_radius() const { return _minorRadius; }

    virtual float distance(const glm::vec3 &pos) const override
    {
        glm::vec3 p = pos - _center;
        glm::vec2 q(glm::length(glm::vec2(p.x, p.z)) - _majorRadius, p.y);
        return glm::length(q) - _minorRadius;
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_center", "center"), &JarTorusSdf::set_center);
        ClassDB::bind_method(D_METHOD("get_center"), &JarTorusSdf::get_center);
        ClassDB::bind_method(D_METHOD("set_major_radius", "r"), &JarTorusSdf::set_major_radius);
        ClassDB::bind_method(D_METHOD("get_major_radius"), &JarTorusSdf::get_major_radius);
        ClassDB::bind_method(D_METHOD("set_minor_radius", "r"), &JarTorusSdf::set_minor_radius);
        ClassDB::bind_method(D_METHOD("get_minor_radius"), &JarTorusSdf::get_minor_radius);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "center"), "set_center", "get_center");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "major_radius"), "set_major_radius", "get_major_radius");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "minor_radius"), "set_minor_radius", "get_minor_radius");
    }
};

#endif // TORUS_SDF_H
