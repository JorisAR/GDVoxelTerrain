#ifndef JAR_BODY_ORBIT_H
#define JAR_BODY_ORBIT_H

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

  protected:
    static void _bind_methods();
    void _notification(int what);

  public:
    JarCelestialBody *get_parent_body() const;
    // Moves to universe time `time`.
    void apply(double time);
    // Position relative to the parent's center at `time`.
    Vector3 offset_at(double time) const;

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
