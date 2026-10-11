#ifndef PLANET_SDF_H
#define PLANET_SDF_H

#include "signed_distance_field.h"
#include <godot_cpp/classes/fast_noise_lite.hpp>
#include <glm/glm.hpp>

class JarPlanetSdf : public JarSignedDistanceField
{
    GDCLASS(JarPlanetSdf, JarSignedDistanceField);

private:
    Ref<FastNoiseLite> _noiseLite;
    glm::vec3 _center = glm::vec3(0.0f);
    float _radius = 1000.0f;
    float _noiseScale = 50.0f;
    const float Epsilon = 0.01f;

public:
    JarPlanetSdf() {}

    void set_noise(Ref<FastNoiseLite> noise) { set_and_notify(_noiseLite, noise); }
    Ref<FastNoiseLite> get_noise() const { return _noiseLite; }

    void set_radius(float value) { set_and_notify(_radius, value); }
    float get_radius() const { return _radius; }

    void set_center(Vector3 center) { set_and_notify(_center, glm::vec3(center.x, center.y, center.z)); }
    Vector3 get_center() const { 
        return Vector3(_center.x, _center.y, _center.z); 
    }

    void set_noise_scale(float scale) { set_and_notify(_noiseScale, scale); }
    float get_noise_scale() const { return _noiseScale; }

protected:
    // Spherical noise displacement using 3D noise
    float get_spherical_displacement(const glm::vec3& pos) const {
        if(_noiseLite.is_null()) return 0.0f;
        
        const float height = _noiseLite->get_noise_3d(
            pos.x,
            pos.y,
            pos.z
        );

        if(height < 0.0f) return 0.5f * height * _noiseScale;
        return (height) * _noiseScale;
    }

    Bounds bounds() const override {
        // Displacement is at most noise_scale (outward).
        const glm::vec3 e(_radius + _noiseScale);
        return Bounds(_center - e, _center + e);
    }

    virtual float distance(const glm::vec3& pos) const override {
        const glm::vec3 to_center = pos - _center;
        const float base_distance = glm::length(to_center) - _radius;
        
        if(base_distance > _noiseScale * 1.25f) {
            // Early exit for points far outside noise influence
            return base_distance;
        }

        const float displacement = get_spherical_displacement(pos);
        
        return base_distance - displacement;
    }

    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("set_noise", "noise"), &JarPlanetSdf::set_noise);
        ClassDB::bind_method(D_METHOD("get_noise"), &JarPlanetSdf::get_noise);
        ClassDB::bind_method(D_METHOD("set_radius", "radius"), &JarPlanetSdf::set_radius);
        ClassDB::bind_method(D_METHOD("get_radius"), &JarPlanetSdf::get_radius);
        ClassDB::bind_method(D_METHOD("set_center", "center"), &JarPlanetSdf::set_center);
        ClassDB::bind_method(D_METHOD("get_center"), &JarPlanetSdf::get_center);
        ClassDB::bind_method(D_METHOD("set_noise_scale", "scale"), &JarPlanetSdf::set_noise_scale);
        ClassDB::bind_method(D_METHOD("get_noise_scale"), &JarPlanetSdf::get_noise_scale);

        ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "noise", PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite"), 
                    "set_noise", "get_noise");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius"), "set_radius", "get_radius");
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "center"), "set_center", "get_center");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "noise_scale"), "set_noise_scale", "get_noise_scale");
    }
};

#endif // PLANET_SDF_H