#ifndef CYLINDER_SDF_H
#define CYLINDER_SDF_H

#include "signed_distance_field.h"

// A capped cylinder standing along the Y axis.
//   radius = tube radius, height = full height (centred on `center`)
class JarCylinderSdf : public JarSignedDistanceField
{
    GDCLASS(JarCylinderSdf, JarSignedDistanceField);

  private:
    glm::vec3 _center{0.0f, 0.0f, 0.0f};
    float _radius = 1.0f;
    float _height = 2.0f;

  public:
    void set_center(const Vector3 &c) { _center = glm::vec3(c.x, c.y, c.z); }
    Vector3 get_center() const { return Vector3(_center.x, _center.y, _center.z); }
    void set_radius(float r) { _radius = r; }
    float get_radius() const { return _radius; }
    void set_height(float h) { _height = h; }
    float get_height() const { return _height; }

    virtual float distance(const glm::vec3 &pos) const override
    {
        glm::vec3 p = pos - _center;
        glm::vec2 d =
            glm::abs(glm::vec2(glm::length(glm::vec2(p.x, p.z)), p.y)) - glm::vec2(_radius, _height * 0.5f);
        return glm::min(glm::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, glm::vec2(0.0f)));
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_center", "center"), &JarCylinderSdf::set_center);
        ClassDB::bind_method(D_METHOD("get_center"), &JarCylinderSdf::get_center);
        ClassDB::bind_method(D_METHOD("set_radius", "r"), &JarCylinderSdf::set_radius);
        ClassDB::bind_method(D_METHOD("get_radius"), &JarCylinderSdf::get_radius);
        ClassDB::bind_method(D_METHOD("set_height", "h"), &JarCylinderSdf::set_height);
        ClassDB::bind_method(D_METHOD("get_height"), &JarCylinderSdf::get_height);
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "center"), "set_center", "get_center");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius"), "set_radius", "get_radius");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height"), "set_height", "get_height");
    }
};

#endif // CYLINDER_SDF_H
