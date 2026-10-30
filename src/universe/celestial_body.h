#ifndef JAR_CELESTIAL_BODY_H
#define JAR_CELESTIAL_BODY_H

#include "universe_math.h"
#include <algorithm>
#include <godot_cpp/classes/node3d.hpp>

using namespace godot;

class JarVoxelVolume;
class JarBodyOrbit;

// A planet, moon or star: the frame (center, up, radius, sea level) and the
// gravity everything attached to it shares. Put its JarVoxelVolume (if it has
// terrain) and its layers (JarBodyWater, JarBodyAtmosphere, JarBodyClouds,
// JarBodyOrbit for moons) under it. With a volume, the shape, radius and sea
// level come from the volume's up field; without one (a distant moon, a sun),
// from this node's own settings.
class JarCelestialBody : public Node3D
{
    GDCLASS(JarCelestialBody, Node3D);

  public:
    enum Shape
    {
        SHAPE_SPHERE = 0,
        SHAPE_PLANE = 1,
    };

    // The body's frame in global space.
    struct Frame
    {
        bool radial = true;
        Vector3 center;            // radial: the center; plane: a point at height 0
        Vector3 axis{0, 1, 0};     // plane: up
        float seaLevel = 0.0f;     // radial: sea level radius; plane: sea height
        float radius = 0.0f;       // where gravity is surface gravity (radial)
        Vector3 up(const Vector3 &p) const
        {
            if (!radial)
                return axis;
            const Vector3 d = p - center;
            const real_t length = d.length();
            return length > 1e-6f ? d / length : axis;
        }
    };

  private:
    Shape _shape = SHAPE_SPHERE;
    float _radius = 0.0f;          // 0: the volume's sea level
    float _seaLevel = 0.0f;        // without a volume: above the radius
    float _surfaceGravity = 9.8f;  // m/s2 at the radius
    float _gravityReach = 0.0f;    // no gravity beyond (0: unlimited)
    float _spinPeriod = 0.0f;      // seconds per turn (0: no spin)
    Vector3 _spinAxis{0, 1, 0};
    JarVoxelVolume *_volume = nullptr; // explicit; else the first child volume
    bool _registered = false;
    // Where the body hangs in the universe (set by JarVoxelUniverse when it
    // registers): its carrier (an orbit or a parent body, else nothing) and its
    // pose relative to the carrier (or in the universe) at time 0.
    Node3D *_carrier = nullptr;
    DTransform _rest;

  protected:
    static void _bind_methods();
    void _notification(int what);

  public:
    Frame get_frame() const;
    JarVoxelVolume *get_terrain() const;
    // This body's own pull at a global position.
    Vector3 gravity_at(const Vector3 &position) const;
    // The spin at universe time `time`: a rotation about the spin axis.
    glm::dmat3 spin_at(double time) const;
    Node3D *get_carrier() const { return _carrier; }
    const DTransform &get_rest() const { return _rest; }
    void set_placement(Node3D *carrier, const DTransform &rest)
    {
        _carrier = carrier;
        _rest = rest;
    }

    Vector3 get_center() const { return get_frame().center; }
    // The center in the body's own space (its volume's center, else its
    // origin), from local transforms only.
    Vector3 get_local_center() const;
    Vector3 get_up_at(const Vector3 &position) const { return get_frame().up(position); }
    float get_effective_radius() const { return get_frame().radius; }
    float get_effective_sea_level() const { return get_frame().seaLevel; }

    int get_shape() const { return _shape; }
    void set_shape(int value) { _shape = Shape(value); }
    float get_radius() const { return _radius; }
    void set_radius(float value) { _radius = std::max(0.0f, value); }
    float get_sea_level() const { return _seaLevel; }
    void set_sea_level(float value) { _seaLevel = value; }
    float get_surface_gravity() const { return _surfaceGravity; }
    void set_surface_gravity(float value) { _surfaceGravity = std::max(0.0f, value); }
    float get_gravity_reach() const { return _gravityReach; }
    void set_gravity_reach(float value) { _gravityReach = std::max(0.0f, value); }
    float get_spin_period() const { return _spinPeriod; }
    void set_spin_period(float value) { _spinPeriod = value; }
    Vector3 get_spin_axis() const { return _spinAxis; }
    void set_spin_axis(const Vector3 &value) { _spinAxis = value; }
    JarVoxelVolume *get_volume() const { return _volume; }
    void set_volume(JarVoxelVolume *value) { _volume = value; }
};

VARIANT_ENUM_CAST(JarCelestialBody::Shape);

#endif // JAR_CELESTIAL_BODY_H
