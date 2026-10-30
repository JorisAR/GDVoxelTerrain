#ifndef JAR_BODY_ORBIT_H
#define JAR_BODY_ORBIT_H

#include "universe_math.h"
#include <algorithm>
#include <godot_cpp/classes/node3d.hpp>

using namespace godot;

class JarCelestialBody;

// Carries its children (a moon: a JarCelestialBody) on a circular orbit
// around the parent body's center, as a function of universe time (so it is
// the same on every machine and never drifts). Not turned by the parent's
// spin. Put it under a JarCelestialBody.
class JarBodyOrbit : public Node3D
{
    GDCLASS(JarBodyOrbit, Node3D);

  private:
    float _radius = 1000.0f;   // meters from the parent's center
    float _period = 600.0f;    // seconds per orbit (negative: the other way)
    Vector3 _axis{0, 1, 0};    // orbit normal (global)
    float _phase = 0.0f;       // degrees along the orbit at time 0
    bool _registered = false;
    glm::dvec3 _restCenter{0.0}; // universe position orbited without a parent body

  protected:
    static void _bind_methods();
    void _notification(int what);

  public:
    JarCelestialBody *get_parent_body() const;
    // Editor preview: places the orbit at `time` around the parent in the scene.
    void apply(double time);
    // Position relative to the parent's center at `time`.
    Vector3 offset_at(double time) const { return DTransform::to(offset(time)); }
    glm::dvec3 offset(double time) const;
    const glm::dvec3 &get_rest_center() const { return _restCenter; }
    void set_rest_center(const glm::dvec3 &value) { _restCenter = value; }

    float get_radius() const { return _radius; }
    void set_radius(float value) { _radius = std::max(0.0f, value); }
    float get_period() const { return _period; }
    void set_period(float value) { _period = value; }
    Vector3 get_axis() const { return _axis; }
    void set_axis(const Vector3 &value) { _axis = value; }
    float get_phase() const { return _phase; }
    void set_phase(float value) { _phase = value; }
};

#endif // JAR_BODY_ORBIT_H
