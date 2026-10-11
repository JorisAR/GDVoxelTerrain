#ifndef CONE_SDF_H
#define CONE_SDF_H

#include "signed_distance_field.h"

// Capped cone along the Y axis: bottom_radius at -height/2, top_radius at
// +height/2 (top_radius 0 makes a pointed cone, equal radii a cylinder).
class JarConeSdf : public JarSignedDistanceField
{
    GDCLASS(JarConeSdf, JarSignedDistanceField);

  private:
    glm::vec3 _center{0.0f};
    float _height = 2.0f;
    float _bottomRadius = 1.0f;
    float _topRadius = 0.0f;

  public:
    void set_center(const Vector3 &c) { set_and_notify(_center, glm::vec3(c.x, c.y, c.z)); }
    Vector3 get_center() const { return Vector3(_center.x, _center.y, _center.z); }
    void set_height(float h) { set_and_notify(_height, std::max(0.0f, h)); }
    float get_height() const { return _height; }
    void set_bottom_radius(float r) { set_and_notify(_bottomRadius, std::max(0.0f, r)); }
    float get_bottom_radius() const { return _bottomRadius; }
    void set_top_radius(float r) { set_and_notify(_topRadius, std::max(0.0f, r)); }
    float get_top_radius() const { return _topRadius; }

    float distance(const glm::vec3 &pos) const override
    {
        // Inigo Quilez, capped cone.
        const glm::vec3 p = pos - _center;
        const float h = _height * 0.5f;
        const float r1 = _bottomRadius, r2 = _topRadius;
        const glm::vec2 q(glm::length(glm::vec2(p.x, p.z)), p.y);
        const glm::vec2 k1(r2, h);
        const glm::vec2 k2(r2 - r1, 2.0f * h);
        const glm::vec2 ca(q.x - std::min(q.x, q.y < 0.0f ? r1 : r2), std::abs(q.y) - h);
        const float k2len = glm::dot(k2, k2);
        const glm::vec2 cb = q - k1 + k2 * (k2len > 0.0f ? glm::clamp(glm::dot(k1 - q, k2) / k2len, 0.0f, 1.0f) : 0.0f);
        const float s = (cb.x < 0.0f && ca.y < 0.0f) ? -1.0f : 1.0f;
        return s * std::sqrt(std::min(glm::dot(ca, ca), glm::dot(cb, cb)));
    }

    Bounds bounds() const override
    {
        const float r = std::max(_bottomRadius, _topRadius);
        const glm::vec3 e(r, _height * 0.5f, r);
        return Bounds(_center - e, _center + e);
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_center", "center"), &JarConeSdf::set_center);
        ClassDB::bind_method(D_METHOD("get_center"), &JarConeSdf::get_center);
        ClassDB::bind_method(D_METHOD("set_height", "height"), &JarConeSdf::set_height);
        ClassDB::bind_method(D_METHOD("get_height"), &JarConeSdf::get_height);
        ClassDB::bind_method(D_METHOD("set_bottom_radius", "radius"), &JarConeSdf::set_bottom_radius);
        ClassDB::bind_method(D_METHOD("get_bottom_radius"), &JarConeSdf::get_bottom_radius);
        ClassDB::bind_method(D_METHOD("set_top_radius", "radius"), &JarConeSdf::set_top_radius);
        ClassDB::bind_method(D_METHOD("get_top_radius"), &JarConeSdf::get_top_radius);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "center"), "set_center", "get_center");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height"), "set_height", "get_height");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bottom_radius"), "set_bottom_radius", "get_bottom_radius");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "top_radius"), "set_top_radius", "get_top_radius");
    }
};

#endif // CONE_SDF_H
