#ifndef BOX_SDF_H
#define BOX_SDF_H

#include "signed_distance_field.h"

// Axis-aligned box with optional rounded edges. extent = half size.
class JarBoxSdf : public JarSignedDistanceField
{
    GDCLASS(JarBoxSdf, JarSignedDistanceField);

  private:
    glm::vec3 _center{0.0f};
    glm::vec3 _extent{1.0f};
    float _rounding = 0.0f;

  public:
    void set_center(const Vector3 &c) { set_and_notify(_center, glm::vec3(c.x, c.y, c.z)); }
    Vector3 get_center() const { return Vector3(_center.x, _center.y, _center.z); }
    void set_extent(const Vector3 &e) { set_and_notify(_extent, glm::max(glm::vec3(e.x, e.y, e.z), glm::vec3(0.0f))); }
    Vector3 get_extent() const { return Vector3(_extent.x, _extent.y, _extent.z); }
    void set_rounding(float r) { set_and_notify(_rounding, std::max(0.0f, r)); }
    float get_rounding() const { return _rounding; }

    float distance(const glm::vec3 &pos) const override
    {
        const float r = std::min(_rounding, std::min(_extent.x, std::min(_extent.y, _extent.z)));
        const glm::vec3 q = glm::abs(pos - _center) - _extent + glm::vec3(r);
        return glm::length(glm::max(q, 0.0f)) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f) - r;
    }

    Bounds bounds() const override
    {
        return Bounds(_center - _extent, _center + _extent);
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_center", "center"), &JarBoxSdf::set_center);
        ClassDB::bind_method(D_METHOD("get_center"), &JarBoxSdf::get_center);
        ClassDB::bind_method(D_METHOD("set_extent", "extent"), &JarBoxSdf::set_extent);
        ClassDB::bind_method(D_METHOD("get_extent"), &JarBoxSdf::get_extent);
        ClassDB::bind_method(D_METHOD("set_rounding", "rounding"), &JarBoxSdf::set_rounding);
        ClassDB::bind_method(D_METHOD("get_rounding"), &JarBoxSdf::get_rounding);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "center"), "set_center", "get_center");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "extent"), "set_extent", "get_extent");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rounding", PROPERTY_HINT_RANGE, "0,64,0.01,or_greater"),
                     "set_rounding", "get_rounding");
    }
};

#endif // BOX_SDF_H
