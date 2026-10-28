#ifndef JAR_VOXEL_UNIVERSE_H
#define JAR_VOXEL_UNIVERSE_H

#include <cstdint>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <vector>

using namespace godot;

class JarCelestialBody;
class JarBodyOrbit;

// Coordinates celestial bodies (JarCelestialBody) at runtime: one clock for
// everything that moves (orbits, spins), gravity summed over all bodies, and
// the anchor body (where the first viewer is) whose gravity the physics space
// uses by default, so plain RigidBody3D / CharacterBody3D fall towards it with
// no setup. An engine singleton; idle (no node in the tree) without bodies.
class JarVoxelUniverse : public Object
{
    GDCLASS(JarVoxelUniverse, Object);

  private:
    static JarVoxelUniverse *s_singleton;
    std::vector<JarCelestialBody *> _bodies;
    std::vector<JarBodyOrbit *> _orbits;
    int64_t _ticks = 0; // physics frames since the start: time is exact and the same everywhere
    uint64_t _anchor = 0;         // instance id
    uint64_t _anchorOverride = 0; // instance id, 0: automatic
    uint64_t _updater = 0;        // instance id of the internal node driving step()
    bool _spaceGravitySet = false;

    void ensure_updater(Node *context);
    JarCelestialBody *choose_anchor() const;
    void apply_space_gravity(Node *context, JarCelestialBody *anchor);

  protected:
    static void _bind_methods();

  public:
    JarVoxelUniverse();
    ~JarVoxelUniverse();
    static JarVoxelUniverse *get_singleton() { return s_singleton; }
    // Project Settings under jar_voxel_terrain/ (registered when the extension loads).
    static void register_settings();

    // Bodies and orbits register themselves while in the tree (not in the editor).
    void register_body(JarCelestialBody *body);
    void unregister_body(JarCelestialBody *body);
    void register_orbit(JarBodyOrbit *orbit);
    void unregister_orbit(JarBodyOrbit *orbit);

    // Physics frame start (the internal node): advances the clock, moves the
    // orbits and spins, picks the anchor and sets the space's gravity.
    void step(Node *context);
    // Back to Godot's own default gravity (no bodies left).
    void restore_space_gravity(Node *context);

    // Seconds of universe time (start time + physics frames, times the time scale).
    double get_time() const;
    int64_t get_ticks() const { return _ticks; }
    void set_ticks(int64_t ticks) { _ticks = ticks; }

    bool has_bodies() const { return !_bodies.empty(); }
    TypedArray<JarCelestialBody> get_bodies() const;
    // Gravity at a global position: summed over all bodies, relative to the
    // anchor's free fall (so far bodies only add tides there), or only the
    // dominant body's (Project Settings). Without bodies: the default gravity.
    Vector3 get_gravity(const Vector3 &position) const;
    // "Up" at a global position: against the bodies' pull, weighted towards
    // the strongest (Project Settings, up sharpness), so a nearby moon does not
    // tilt it; smooth in between. `fallback` where there is no pull at all.
    Vector3 get_up(const Vector3 &position, const Vector3 &fallback = Vector3(0, 1, 0)) const;
    // The body pulling hardest at a global position (null without bodies).
    JarCelestialBody *get_dominant_body(const Vector3 &position) const;
    // The body physics is relative to: the explicit anchor if set, else the
    // dominant body at the first viewer (or camera).
    JarCelestialBody *get_anchor() const;
    void set_anchor_override(JarCelestialBody *body);
    JarCelestialBody *get_anchor_override() const;
};

// Internal: drives JarVoxelUniverse::step() at the start of each physics frame.
class JarVoxelUniverseUpdater : public Node
{
    GDCLASS(JarVoxelUniverseUpdater, Node);

  protected:
    static void _bind_methods()
    {
    }
    void _notification(int what);
};

#endif // JAR_VOXEL_UNIVERSE_H
