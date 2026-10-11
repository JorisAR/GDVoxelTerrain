#ifndef ELLIPSOID_SDF_H
#define ELLIPSOID_SDF_H

#include "signed_distance_field.h"

// Ellipsoid with per-axis radii. The distance is a (close) bound rather than
// exact, which is standard for ellipsoids and fine for meshing.
class JarEllipsoidSdf : public JarSignedDistanceField
{
    GDCLASS(JarEllipsoidSdf, JarSignedDistanceField);

  private:
    glm::vec3 _center{0.0f};
    glm::vec3 _radii{1.0f, 0.5f, 1.0f};

  public:
    void set_center(const Vector3 &c) { set_and_notify(_center, glm::vec3(c.x, c.y, c.z)); }
    Vector3 get_center() const { return Vector3(_center.x, _center.y, _center.z); }
    void set_radii(const Vector3 &r)
    {
        set_and_notify(_radii, glm::max(glm::vec3(r.x, r.y, r.z), glm::vec3(1e-4f)));
    }
    Vector3 get_radii() const { return Vector3(_radii.x, _radii.y, _radii.z); }

    float distance(const glm::vec3 &pos) const override
    {
        // Inigo Quilez, ellipsoid (bound).
        const glm::vec3 p = pos - _center;
        const float k0 = glm::length(p / _radii);
        const float k1 = glm::length(p / (_radii * _radii));
        return k1 > 0.0f ? k0 * (k0 - 1.0f) / k1 : -std::min(_radii.x, std::min(_radii.y, _radii.z));
    }

    Bounds bounds() const override
    {
        return Bounds(_center - _radii, _center + _radii);
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_center", "center"), &JarEllipsoidSdf::set_center);
        ClassDB::bind_method(D_METHOD("get_center"), &JarEllipsoidSdf::get_center);
        ClassDB::bind_method(D_METHOD("set_radii", "radii"), &JarEllipsoidSdf::set_radii);
        ClassDB::bind_method(D_METHOD("get_radii"), &JarEllipsoidSdf::get_radii);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "center"), "set_center", "get_center");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "radii"), "set_radii", "get_radii");
    }
};

#endif // ELLIPSOID_SDF_H
