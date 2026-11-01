#ifndef JAR_VOXEL_UNIVERSE_H
#define JAR_VOXEL_UNIVERSE_H

#include "universe_math.h"
#include <cstdint>
#include <unordered_map>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
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
//
// Bodies are placed from universe time in double precision, never stepped, so
// nothing drifts: U(body, t) through their orbits and spins. The scene shows
// that universe from the anchor (frame switching): the anchor stands still and
// everything else moves around it, so physics near the viewer happens on a
// body that does not move. When the anchor changes, the new one stops where it
// is (nothing jumps) and free physics bodies get their velocities converted
// into its frame.
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
    uint64_t _gravityArea = 0; // instance id of the Area3D carrying the anchor's gravity
    // Frame switching: the scene is view(t) * U(body, t), where view(t) keeps
    // the frame anchor at the scene pose it had when it became the anchor.
    uint64_t _frameAnchor = 0;   // instance id
    bool _frameRotating = true;  // turns with the anchor (near it), else only follows its center
    DTransform _frameAnchorPose; // its pose in the scene
    DTransform _lastView;        // view(t) of the last step (kept without an anchor)
    // What each body / orbit was last placed at (instance id): unchanged
    // placements are not applied again (moving a body notifies all its chunks).
    std::unordered_map<uint64_t, Transform3D> _placed;
    // The sky and the lights outside bodies (the sun) turn with the view, as
    // the rest of the universe does; their poses in the universe (instance id).
    std::unordered_map<uint64_t, Basis> _skyRest;
    bool _skyFound = false;
    // A new set of bodies starts its clock at 0 and shows its first anchor as
    // placed in the scene (the scene is the universe at that time).
    bool _viewStarted = false;
    bool _skyStarted = false; // found the sky since the universe started
    double _startTime = 0.0;

    void ensure_updater(Node *context);
    JarCelestialBody *choose_anchor() const;
    // Where the anchor is chosen from: the first viewer, else the camera.
    bool viewer_position(Vector3 &at) const;
    bool choose_rotating(const JarCelestialBody *anchor) const;
    DTransform frame_transform(const JarCelestialBody *anchor, bool rotating, double time) const;
    void find_sky(Node *context);
    void place_sky(Node *context, const DTransform &view, bool restore = false);
    void apply_space_gravity(Node *context, JarCelestialBody *anchor);
    void switch_frame(Node *context, JarCelestialBody *anchor, bool rotating, double time);
    // Floating origin (see floating_origin.h): moves the view (and everything
    // else in the scene) back near the origin once the viewer is far from it.
    void shift_origin_if_far(Node *context, double time);
    // Clip planes that follow the camera's altitude (see adapt_camera).
    std::unordered_map<uint64_t, float> _cameraNear; // each camera's own near plane (instance id)
    void adapt_camera(Node *context);
    bool _alignPending = false; // a new turning frame: its anchor still to be aligned
    void place(double time);
    bool place_node(Node3D *node, const Transform3D &transform); // whether it moved it
    // Velocity and spin (per real second) of something whose scene pose went
    // from `before` to `now` over the last tick.
    Vector3 point_velocity(const DTransform &now, const DTransform &before, const Vector3 &position) const;
    Vector3 angular_velocity(const DTransform &now, const DTransform &before) const;
    double tick_time() const; // universe seconds per physics tick

  protected:
    static void _bind_methods();

  public:
    JarVoxelUniverse();
    ~JarVoxelUniverse();
    static JarVoxelUniverse *get_singleton() { return s_singleton; }
    // Project Settings under jar_voxel_terrain/ (registered when the extension loads).
    static void register_settings();

    // Physics bodies in this group are left alone by anchor switches (they
    // convert their velocities themselves, e.g. the demo player, which moves
    // along with the body it stands on).
    static constexpr const char *FrameExemptGroup = "jar_frame_exempt";

    // Bodies and orbits register themselves while in the tree (not in the editor).
    void register_body(JarCelestialBody *body);
    void unregister_body(JarCelestialBody *body);
    void register_orbit(JarBodyOrbit *orbit);
    void unregister_orbit(JarBodyOrbit *orbit);

    // End of each physics frame (the internal node): advances the clock, picks
    // the anchor (switching frames), places the bodies and sets the space's
    // gravity, so colliders are in place for the next physics step.
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
    // `relative_to`: for something moving along with that body instead (riding
    // a body that is not the anchor): relative to its free fall.
    // `frame_relative` false: the bodies' pulls as they are, with no frame
    // correction: smooth everywhere and the same across frame switches (for
    // orientation in free flight, not for motion).
    Vector3 get_gravity(const Vector3 &position, JarCelestialBody *relative_to = nullptr,
                        bool frame_relative = true) const;
    // "Up" at a global position: against the bodies' pull, weighted towards
    // the strongest (Project Settings, up sharpness), so a nearby moon does not
    // tilt it; smooth in between. `fallback` where there is no pull at all.
    Vector3 get_up(const Vector3 &position, const Vector3 &fallback = Vector3(0, 1, 0),
                   JarCelestialBody *relative_to = nullptr, bool frame_relative = true) const;
    // The body pulling hardest at a global position (null without bodies).
    JarCelestialBody *get_dominant_body(const Vector3 &position) const;
    // The body physics is relative to: the explicit anchor if set, else the
    // dominant body at the first viewer (or camera).
    JarCelestialBody *get_anchor() const;
    void set_anchor_override(JarCelestialBody *body);
    JarCelestialBody *get_anchor_override() const;

    // Universe pose of a body / an orbit at `time` (doubles; not scene space).
    DTransform universe_transform(const JarCelestialBody *body, double time) const;
    DTransform orbit_transform(const JarBodyOrbit *orbit, double time) const;
    // Scene = view(time) * universe.
    DTransform view_at(double time) const;
    // Scene velocity of a point at `position` carried by `body` (its orbit and
    // spin, seen from the anchor), and the body's angular velocity in the scene.
    Vector3 get_body_velocity_at(JarCelestialBody *body, const Vector3 &position) const;
    Vector3 get_body_angular_velocity(JarCelestialBody *body) const;
    // How `body` moved in the scene over the last physics tick, computed in
    // double precision: exactly the identity for the anchor the scene turns
    // with. For controllers that move along with a body (apply it to their
    // transform each tick); differencing its global transforms instead
    // rounds at the scale of its distance (centimeters on a 600 km planet).
    Transform3D get_body_motion(JarCelestialBody *body) const;
    // The body the scene is currently relative to: its center stands still,
    // and near it (its rotating frame height) it does not turn either.
    JarCelestialBody *get_frame_anchor() const;
    bool is_frame_rotating() const { return _frameRotating; }
    // Signals: anchor_changed(previous, current) when the anchor body changes;
    // frame_changed(anchor, rotating) on every switch (also between turning
    // with the anchor and not), after free bodies' velocities were converted.
};

// Internal: drives JarVoxelUniverse::step() at the end of each physics frame.
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
