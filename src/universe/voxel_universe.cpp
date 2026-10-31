#include "voxel_universe.h"
#include "body_layer.h"
#include "body_orbit.h"
#include "celestial_body.h"
#include "voxel_viewer.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/world3d.hpp>

JarVoxelUniverse *JarVoxelUniverse::s_singleton = nullptr;

namespace
{
const char *TimeScale = "jar_voxel_terrain/universe/time_scale";
const char *StartTime = "jar_voxel_terrain/universe/start_time";
const char *PhysicalStrength = "jar_voxel_terrain/gravity/physical_strength";
const char *UpSharpness = "jar_voxel_terrain/gravity/up_sharpness";
const char *SpaceGravity = "jar_voxel_terrain/gravity/drive_space_gravity";
const char *FrameSwitching = "jar_voxel_terrain/universe/frame_switching";

Variant setting(const char *name, const Variant &fallback)
{
    ProjectSettings *settings = ProjectSettings::get_singleton();
    return settings && settings->has_setting(name) ? settings->get_setting_with_override(name) : fallback;
}

void add_setting(const char *name, const Variant &value, Variant::Type type, PropertyHint hint = PROPERTY_HINT_NONE,
                 const String &hintString = "")
{
    ProjectSettings *settings = ProjectSettings::get_singleton();
    if (!settings->has_setting(name))
        settings->set_setting(name, value);
    settings->set_initial_value(name, value);
    settings->set_as_basic(name, true);
    Dictionary info;
    info["name"] = name;
    info["type"] = type;
    info["hint"] = hint;
    info["hint_string"] = hintString;
    settings->add_property_info(info);
}

int depth(const Node *node)
{
    int d = 0;
    for (const Node *n = node; n != nullptr; n = n->get_parent())
        d++;
    return d;
}
} // namespace

void JarVoxelUniverse::register_settings()
{
    add_setting(TimeScale, 1.0, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1000,0.01,or_greater");
    add_setting(StartTime, 0.0, Variant::FLOAT, PROPERTY_HINT_NONE, "suffix:s");
    add_setting(PhysicalStrength, true, Variant::BOOL);
    add_setting(UpSharpness, 4.0, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,16,0.1");
    add_setting(SpaceGravity, true, Variant::BOOL);
    add_setting(FrameSwitching, true, Variant::BOOL);
}

JarVoxelUniverse::JarVoxelUniverse()
{
    s_singleton = this;
}

JarVoxelUniverse::~JarVoxelUniverse()
{
    if (s_singleton == this)
        s_singleton = nullptr;
}

void JarVoxelUniverse::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_time"), &JarVoxelUniverse::get_time);
    ClassDB::bind_method(D_METHOD("get_ticks"), &JarVoxelUniverse::get_ticks);
    ClassDB::bind_method(D_METHOD("set_ticks", "ticks"), &JarVoxelUniverse::set_ticks);
    ClassDB::bind_method(D_METHOD("has_bodies"), &JarVoxelUniverse::has_bodies);
    ClassDB::bind_method(D_METHOD("get_bodies"), &JarVoxelUniverse::get_bodies);
    ClassDB::bind_method(D_METHOD("get_gravity", "global_position", "relative_to", "frame_relative"),
                         &JarVoxelUniverse::get_gravity, DEFVAL(Variant()), DEFVAL(true));
    ClassDB::bind_method(D_METHOD("get_up", "global_position", "fallback", "relative_to", "frame_relative"),
                         &JarVoxelUniverse::get_up, DEFVAL(Vector3(0, 1, 0)), DEFVAL(Variant()), DEFVAL(true));
    ClassDB::bind_method(D_METHOD("get_dominant_body", "global_position"), &JarVoxelUniverse::get_dominant_body);
    ClassDB::bind_method(D_METHOD("get_anchor"), &JarVoxelUniverse::get_anchor);
    ClassDB::bind_method(D_METHOD("set_anchor_override", "body"), &JarVoxelUniverse::set_anchor_override);
    ClassDB::bind_method(D_METHOD("get_anchor_override"), &JarVoxelUniverse::get_anchor_override);
    ClassDB::bind_method(D_METHOD("get_frame_anchor"), &JarVoxelUniverse::get_frame_anchor);
    ClassDB::bind_method(D_METHOD("is_frame_rotating"), &JarVoxelUniverse::is_frame_rotating);
    ClassDB::bind_method(D_METHOD("get_body_velocity_at", "body", "global_position"),
                         &JarVoxelUniverse::get_body_velocity_at);
    ClassDB::bind_method(D_METHOD("get_body_angular_velocity", "body"), &JarVoxelUniverse::get_body_angular_velocity);
    ADD_SIGNAL(MethodInfo("anchor_changed",
                          PropertyInfo(Variant::OBJECT, "previous", PROPERTY_HINT_NODE_TYPE, "JarCelestialBody"),
                          PropertyInfo(Variant::OBJECT, "current", PROPERTY_HINT_NODE_TYPE, "JarCelestialBody")));
    ADD_SIGNAL(MethodInfo("frame_changed",
                          PropertyInfo(Variant::OBJECT, "anchor", PROPERTY_HINT_NODE_TYPE, "JarCelestialBody"),
                          PropertyInfo(Variant::BOOL, "rotating")));
}

void JarVoxelUniverse::register_body(JarCelestialBody *body)
{
    if (_bodies.empty())
    {
        // A new universe (a scene loaded): its clock starts now, from the
        // scene as placed. set_ticks() still sets any time after this.
        _ticks = 0;
        _startTime = get_time();
        _viewStarted = false;
        _skyStarted = false;
    }
    // Where it hangs: relative to its carrier (the nearest orbit or body
    // above it), else its universe pose as placed in the scene now.
    Node3D *carrier = nullptr;
    Transform3D local = body->get_transform();
    for (Node *n = body->get_parent(); n != nullptr; n = n->get_parent())
    {
        if (Object::cast_to<JarBodyOrbit>(n) || Object::cast_to<JarCelestialBody>(n))
        {
            carrier = Object::cast_to<Node3D>(n);
            break;
        }
        if (const Node3D *n3 = Object::cast_to<Node3D>(n))
            local = n3->get_transform() * local;
    }
    body->set_placement(carrier, carrier ? DTransform::from(local)
                                         : _lastView.inverse() * DTransform::from(body->get_global_transform()));
    if (std::find(_bodies.begin(), _bodies.end(), body) == _bodies.end())
        _bodies.push_back(body);
    // Parents first: placing a parent moves its children.
    std::sort(_bodies.begin(), _bodies.end(),
              [](const JarCelestialBody *a, const JarCelestialBody *b) { return depth(a) < depth(b); });
    // In place right away (a moon on its orbit), before anything looks at it.
    place_node(body, (view_at(get_time()) * universe_transform(body, get_time())).to());
    ensure_updater(body);
    // Anchor and gravity right away: the updater only starts next frame.
    if (get_anchor() == nullptr)
    {
        JarCelestialBody *anchor = choose_anchor();
        _anchor = anchor ? anchor->get_instance_id() : 0;
        if (bool(setting(SpaceGravity, true)))
            apply_space_gravity(body, anchor);
    }
}

void JarVoxelUniverse::unregister_body(JarCelestialBody *body)
{
    _bodies.erase(std::remove(_bodies.begin(), _bodies.end(), body), _bodies.end());
    if (_anchor == body->get_instance_id())
        _anchor = 0;
    if (_anchorOverride == body->get_instance_id())
        _anchorOverride = 0;
    if (_frameAnchor == body->get_instance_id())
        _frameAnchor = 0; // the view stays as it was until the next anchor
    _placed.erase(body->get_instance_id());
    if (_bodies.empty())
    {
        if (auto *updater = Object::cast_to<Node>(ObjectDB::get_instance(_updater)))
        {
            restore_space_gravity(updater);
            place_sky(updater, DTransform(), true);
            updater->queue_free();
            _updater = 0;
        }
        _lastView = DTransform(); // the next bodies start from the scene as placed
        _skyRest.clear();
        _skyFound = false;
    }
}

void JarVoxelUniverse::register_orbit(JarBodyOrbit *orbit)
{
    // Without a body to orbit: around where its parent is now.
    if (const Node3D *parent = Object::cast_to<Node3D>(orbit->get_parent()); parent && !orbit->get_parent_body())
        orbit->set_rest_center(_lastView.inverse().xform(DTransform::from(parent->get_global_position())));
    if (std::find(_orbits.begin(), _orbits.end(), orbit) == _orbits.end())
        _orbits.push_back(orbit);
    // Parents first: a moon's orbit is around where its planet is now.
    std::sort(_orbits.begin(), _orbits.end(), [](const JarBodyOrbit *a, const JarBodyOrbit *b) {
        return depth(a) < depth(b);
    });
    place_node(orbit, (view_at(get_time()) * orbit_transform(orbit, get_time())).to());
}

void JarVoxelUniverse::unregister_orbit(JarBodyOrbit *orbit)
{
    _orbits.erase(std::remove(_orbits.begin(), _orbits.end(), orbit), _orbits.end());
    _placed.erase(orbit->get_instance_id());
}

void JarVoxelUniverse::ensure_updater(Node *context)
{
    if (ObjectDB::get_instance(_updater) != nullptr || context->get_tree() == nullptr)
        return;
    JarVoxelUniverseUpdater *updater = memnew(JarVoxelUniverseUpdater);
    updater->set_name("JarVoxelUniverseUpdater");
    _updater = updater->get_instance_id();
    // Deferred: the tree is busy adding the body.
    context->get_tree()->get_root()->call_deferred("add_child", updater);
}

double JarVoxelUniverse::get_time() const
{
    const double start = double(setting(StartTime, 0.0));
    const double scale = double(setting(TimeScale, 1.0));
    const double rate = double(Engine::get_singleton()->get_physics_ticks_per_second());
    return start + double(_ticks) * scale / std::max(rate, 1.0);
}

void JarVoxelUniverse::step(Node *context)
{
    _ticks++;
    const double time = get_time();
    // Placed first, so the anchor is chosen from where the bodies are now. A
    // switch keeps this placement (the new anchor stops where it is). Before
    // the first anchor, the scene as placed (the universe at its start) is
    // where they are: placing them without a view would move them anywhere.
    if (_viewStarted || !bool(setting(FrameSwitching, true)))
        place(time);
    JarCelestialBody *anchor = choose_anchor();
    _anchor = anchor ? anchor->get_instance_id() : 0;
    if (!bool(setting(FrameSwitching, true)))
        _frameAnchor = 0;
    else if (anchor)
    {
        const bool rotating = choose_rotating(anchor);
        if (anchor->get_instance_id() != _frameAnchor || rotating != _frameRotating)
        {
            switch_frame(context, anchor, rotating, time);
            place(time); // unchanged, except at the very first anchor
        }
    }
    if (!_skyFound)
        find_sky(context);
    place_sky(context, _lastView);
    if (bool(setting(SpaceGravity, true)))
        apply_space_gravity(context, anchor);
}

double JarVoxelUniverse::tick_time() const
{
    const double rate = double(Engine::get_singleton()->get_physics_ticks_per_second());
    return double(setting(TimeScale, 1.0)) / std::max(rate, 1.0);
}

DTransform JarVoxelUniverse::orbit_transform(const JarBodyOrbit *orbit, double time) const
{
    glm::dvec3 center = orbit->get_rest_center();
    if (const JarCelestialBody *parent = orbit->get_parent_body())
    {
        // The parent's center (e.g. its volume's) where its universe pose puts it.
        center = universe_transform(parent, time).xform(DTransform::from(parent->get_local_center()));
    }
    // Not turned by the parent: an orbit's axis is fixed in the universe.
    return DTransform::translation(center + orbit->offset(time));
}

DTransform JarVoxelUniverse::universe_transform(const JarCelestialBody *body, double time) const
{
    const DTransform &rest = body->get_rest();
    const DTransform local(body->spin_at(time) * rest.basis, rest.origin);
    if (const auto *orbit = Object::cast_to<JarBodyOrbit>(body->get_carrier()))
        return orbit_transform(orbit, time) * local;
    if (const auto *parent = Object::cast_to<JarCelestialBody>(body->get_carrier()))
        return universe_transform(parent, time) * local;
    return local;
}

JarCelestialBody *JarVoxelUniverse::get_frame_anchor() const
{
    return Object::cast_to<JarCelestialBody>(ObjectDB::get_instance(_frameAnchor));
}

DTransform JarVoxelUniverse::frame_transform(const JarCelestialBody *anchor, bool rotating, double time) const
{
    const DTransform pose = universe_transform(anchor, time);
    // Not rotating: only the center (where the body's local origin is).
    return rotating ? pose : DTransform::translation(pose.origin);
}

DTransform JarVoxelUniverse::view_at(double time) const
{
    if (const JarCelestialBody *anchor = get_frame_anchor())
        return _frameAnchorPose * frame_transform(anchor, _frameRotating, time).inverse();
    return _lastView;
}

void JarVoxelUniverse::place(double time)
{
    const DTransform view = view_at(time);
    _lastView = view;
    // A turning frame's anchor stands still: exactly at its pose (no rounding).
    JarCelestialBody *still = _frameRotating ? get_frame_anchor() : nullptr;
    // Parents first (orbits are top level, so only their bodies' order matters).
    // An orbit carrying a still anchor stays where it is: its bodies are placed
    // in the scene directly, and a moving parent would move all their chunks.
    for (JarBodyOrbit *orbit : _orbits)
        if (still == nullptr || !orbit->is_ancestor_of(still))
            place_node(orbit, (view * orbit_transform(orbit, time)).to());
    for (JarCelestialBody *body : _bodies)
        place_node(body, body == still ? _frameAnchorPose.to() : (view * universe_transform(body, time)).to());
}

void JarVoxelUniverse::place_node(Node3D *node, const Transform3D &transform)
{
    auto [it, added] = _placed.try_emplace(node->get_instance_id(), transform);
    if (!added && it->second == transform)
        return;
    it->second = transform;
    node->set_global_transform(transform);
}

void JarVoxelUniverse::switch_frame(Node *context, JarCelestialBody *anchor, bool rotating, double time)
{
    JarCelestialBody *previous = get_frame_anchor();
    const DTransform oldView = view_at(time);
    // The new frame stops where the old view shows it now: nothing jumps. The
    // very first one stays as placed in the scene (where the universe was at
    // its start), however far its orbit has carried it since.
    const double shown = _viewStarted ? time : _startTime;
    const DTransform now = oldView * frame_transform(anchor, rotating, shown);
    const DTransform before = view_at(shown - tick_time()) * frame_transform(anchor, rotating, shown - tick_time());
    _viewStarted = true;
    _frameAnchor = anchor->get_instance_id();
    _frameRotating = rotating;
    _frameAnchorPose = now;
    _skyFound = false; // pick up lights added since
    // Free physics bodies keep moving as they did, now seen from a frame that
    // moved (and turned) with the new one (not at the very first anchor: the
    // scene was placed at rest).
    if (previous != nullptr && context->is_inside_tree())
    {
        const Vector3 spin = angular_velocity(now, before);
        Node *root = context->get_tree()->get_root();
        for (const char *type : {"RigidBody3D", "CharacterBody3D"})
        {
            TypedArray<Node> found = root->find_children("*", type, true, false);
            for (int64_t i = 0; i < found.size(); ++i)
            {
                Node *node = Object::cast_to<Node>(found[i]);
                if (node == nullptr || node->is_in_group(FrameExemptGroup))
                    continue;
                if (auto *rigid = Object::cast_to<RigidBody3D>(node))
                {
                    if (rigid->is_freeze_enabled())
                        continue;
                    const Vector3 carried = point_velocity(now, before, rigid->get_global_position());
                    rigid->set_linear_velocity(rigid->get_linear_velocity() - carried);
                    rigid->set_angular_velocity(rigid->get_angular_velocity() - spin);
                }
                else if (auto *character = Object::cast_to<CharacterBody3D>(node))
                {
                    const Vector3 carried = point_velocity(now, before, character->get_global_position());
                    character->set_velocity(character->get_velocity() - carried);
                }
            }
        }
    }
    if (previous != anchor)
        emit_signal("anchor_changed", previous, anchor);
    emit_signal("frame_changed", anchor, rotating);
}

void JarVoxelUniverse::find_sky(Node *context)
{
    if (!context->is_inside_tree())
        return;
    _skyFound = true;
    // The sky and lights are as placed in the scene, which shows the universe
    // as it was at its start (the first time), or as it is shown now.
    const Basis toUniverse = (_skyStarted ? _lastView : view_at(_startTime)).inverse().to().basis;
    _skyStarted = true;
    const Ref<World3D> world = context->get_viewport()->find_world_3d();
    if (world.is_valid() && world->get_environment().is_valid())
    {
        const Ref<Environment> environment = world->get_environment();
        _skyRest.try_emplace(environment->get_instance_id(),
                             toUniverse * Basis::from_euler(environment->get_sky_rotation()));
    }
    // Lights not on a body and not driven by a star: other sky lights.
    TypedArray<Node> lights = context->get_tree()->get_root()->find_children("*", "DirectionalLight3D", true, false);
    for (int64_t i = 0; i < lights.size(); ++i)
    {
        auto *light = Object::cast_to<DirectionalLight3D>(lights[i]);
        bool onBody = false;
        for (Node *n = light ? light->get_parent() : nullptr; n != nullptr && !onBody; n = n->get_parent())
            onBody = Object::cast_to<JarCelestialBody>(n) != nullptr;
        if (light && !onBody && !light->is_in_group(JarBodyLight::LightGroup))
            _skyRest.try_emplace(light->get_instance_id(), toUniverse * light->get_global_basis());
    }
}

void JarVoxelUniverse::place_sky(Node *context, const DTransform &view, bool restore)
{
    // The view's rotation only (bases are orthonormal; no scale in a view).
    const Basis turn = view.to().basis.orthonormalized();
    for (auto it = _skyRest.begin(); it != _skyRest.end();)
    {
        Object *object = ObjectDB::get_instance(it->first);
        if (object == nullptr)
        {
            it = _skyRest.erase(it);
            continue;
        }
        const Basis basis = restore ? it->second : turn * it->second;
        if (auto *environment = Object::cast_to<Environment>(object))
        {
            const Vector3 euler = basis.get_euler();
            if (environment->get_sky_rotation() != euler)
            {
                environment->set_sky_rotation(euler);
                // Sky shaders get their view direction turned (EYEDIR) but not
                // the lights (LIGHT0_DIRECTION): this turns it back for them.
                if (const Ref<Sky> sky = environment->get_sky(); sky.is_valid())
                    if (const Ref<ShaderMaterial> material = sky->get_material(); material.is_valid())
                        material->set_shader_parameter("jar_sky_basis", Basis::from_euler(euler));
            }
        }
        else if (auto *light = Object::cast_to<DirectionalLight3D>(object); light && light->is_inside_tree())
        {
            if (light->is_in_group(JarBodyLight::LightGroup))
            {
                it = _skyRest.erase(it); // a star drives it now
                continue;
            }
            if (light->get_global_basis() != basis)
                light->set_global_basis(basis);
        }
        ++it;
    }
}

Vector3 JarVoxelUniverse::point_velocity(const DTransform &now, const DTransform &before, const Vector3 &position) const
{
    const glm::dvec3 p = DTransform::from(position);
    const glm::dvec3 carried = (before * now.inverse()).xform(p); // where the point was a tick ago
    const double seconds = 1.0 / std::max(double(Engine::get_singleton()->get_physics_ticks_per_second()), 1.0);
    return DTransform::to((p - carried) / seconds);
}

Vector3 JarVoxelUniverse::angular_velocity(const DTransform &now, const DTransform &before) const
{
    // The rotation over the last tick, as axis * angle (m[column][row]).
    const glm::dmat3 r = now.basis * glm::inverse(before.basis);
    const glm::dvec3 v(r[1][2] - r[2][1], r[2][0] - r[0][2], r[0][1] - r[1][0]);
    const double s = 0.5 * glm::length(v);
    const double angle = std::atan2(s, 0.5 * (r[0][0] + r[1][1] + r[2][2] - 1.0));
    const glm::dvec3 axisAngle = s > 1e-15 ? v * (0.5 * angle / s) : 0.5 * v;
    const double seconds = 1.0 / std::max(double(Engine::get_singleton()->get_physics_ticks_per_second()), 1.0);
    return DTransform::to(axisAngle / seconds);
}

Vector3 JarVoxelUniverse::get_body_velocity_at(JarCelestialBody *body, const Vector3 &position) const
{
    if (body == nullptr)
        return Vector3();
    const double time = get_time();
    const DTransform now = view_at(time) * universe_transform(body, time);
    const DTransform before = view_at(time - tick_time()) * universe_transform(body, time - tick_time());
    return point_velocity(now, before, position);
}

Vector3 JarVoxelUniverse::get_body_angular_velocity(JarCelestialBody *body) const
{
    if (body == nullptr)
        return Vector3();
    const double time = get_time();
    const DTransform now = view_at(time) * universe_transform(body, time);
    const DTransform before = view_at(time - tick_time()) * universe_transform(body, time - tick_time());
    return angular_velocity(now, before);
}

JarCelestialBody *JarVoxelUniverse::choose_anchor() const
{
    if (auto *body = Object::cast_to<JarCelestialBody>(ObjectDB::get_instance(_anchorOverride)))
        return body;
    Vector3 at;
    const bool found = viewer_position(at);
    auto *current = Object::cast_to<JarCelestialBody>(ObjectDB::get_instance(_anchor));
    if (found)
    {
        // The smallest sphere of influence the viewer is in (a moon's inside
        // its planet's): well before its surface, where its gravity is still
        // weaker than the planet's. A little hysteresis on leaving it.
        JarCelestialBody *best = nullptr;
        float bestReach = INFINITY;
        for (JarCelestialBody *body : _bodies)
        {
            const float sphere = body->get_sphere_of_influence();
            const float reach = body == current ? 1.1f * sphere : sphere;
            if (std::isfinite(sphere) && reach < bestReach && (at - body->get_center()).length() < reach)
            {
                best = body;
                bestReach = reach;
            }
        }
        if (best)
            return best;
    }
    // Else among the bodies on no orbit: the strongest pull.
    JarCelestialBody *dominant = nullptr;
    real_t strength = -1.0f;
    for (JarCelestialBody *body : _bodies)
        if (!std::isfinite(body->get_sphere_of_influence()) && body->gravity_at(at).length() > strength)
        {
            dominant = body;
            strength = body->gravity_at(at).length();
        }
    if (!found || dominant == nullptr || current == nullptr || dominant == current ||
        std::isfinite(current->get_sphere_of_influence()))
        return dominant ? dominant : current;
    // A little hysteresis: no flipping back and forth at the balance point.
    return strength > 1.25f * current->gravity_at(at).length() ? dominant : current;
}

bool JarVoxelUniverse::viewer_position(Vector3 &at) const
{
    for (const JarVoxelViewer *viewer : JarVoxelViewer::get_viewers())
        if (viewer->is_enabled() && viewer->is_inside_tree())
        {
            at = viewer->get_global_position();
            return true;
        }
    auto *updater = Object::cast_to<Node>(ObjectDB::get_instance(_updater));
    Viewport *viewport = updater && updater->is_inside_tree() ? updater->get_viewport() : nullptr;
    if (Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr)
    {
        at = camera->get_global_position();
        return true;
    }
    return false;
}

bool JarVoxelUniverse::choose_rotating(const JarCelestialBody *anchor) const
{
    Vector3 at;
    if (!viewer_position(at))
        return true;
    // Near the surface the ground stands still (and the sky turns); higher up
    // the sky stands still (and the body turns below). A little hysteresis.
    const float height = anchor->get_effective_rotating_frame_height();
    const bool staying = anchor->get_instance_id() == _frameAnchor && _frameRotating;
    return anchor->get_altitude(at) < (staying ? 1.2f * height : height);
}

void JarVoxelUniverse::apply_space_gravity(Node *context, JarCelestialBody *anchor)
{
    if (anchor == nullptr || context == nullptr || !context->is_inside_tree())
        return;
    const Ref<World3D> world = context->get_viewport()->find_world_3d();
    if (world.is_null())
        return;
    PhysicsServer3D *physics = PhysicsServer3D::get_singleton();
    const RID space = world->get_space();
    const JarCelestialBody::Frame frame = anchor->get_frame();
    // The space's default area: plain bodies get the anchor's gravity with no
    // setup (inverse square from its center, surface gravity at its radius).
    physics->area_set_param(space, PhysicsServer3D::AREA_PARAM_GRAVITY, anchor->get_surface_gravity());
    if (frame.radial)
    {
        physics->area_set_param(space, PhysicsServer3D::AREA_PARAM_GRAVITY_IS_POINT, true);
        physics->area_set_param(space, PhysicsServer3D::AREA_PARAM_GRAVITY_VECTOR, frame.center);
        physics->area_set_param(space, PhysicsServer3D::AREA_PARAM_GRAVITY_POINT_UNIT_DISTANCE,
                                std::max(frame.radius, 1.0f));
    }
    else
    {
        physics->area_set_param(space, PhysicsServer3D::AREA_PARAM_GRAVITY_IS_POINT, false);
        physics->area_set_param(space, PhysicsServer3D::AREA_PARAM_GRAVITY_VECTOR, -frame.axis);
    }
    _spaceGravitySet = true;
}

void JarVoxelUniverse::restore_space_gravity(Node *context)
{
    if (!_spaceGravitySet || context == nullptr || !context->is_inside_tree())
        return;
    const Ref<World3D> world = context->get_viewport()->find_world_3d();
    if (world.is_null())
        return;
    PhysicsServer3D *physics = PhysicsServer3D::get_singleton();
    const RID space = world->get_space();
    physics->area_set_param(space, PhysicsServer3D::AREA_PARAM_GRAVITY, setting("physics/3d/default_gravity", 9.8));
    physics->area_set_param(space, PhysicsServer3D::AREA_PARAM_GRAVITY_IS_POINT, false);
    physics->area_set_param(space, PhysicsServer3D::AREA_PARAM_GRAVITY_VECTOR,
                            setting("physics/3d/default_gravity_vector", Vector3(0, -1, 0)));
    _spaceGravitySet = false;
}

TypedArray<JarCelestialBody> JarVoxelUniverse::get_bodies() const
{
    TypedArray<JarCelestialBody> result;
    for (JarCelestialBody *body : _bodies)
        result.push_back(body);
    return result;
}

Vector3 JarVoxelUniverse::get_gravity(const Vector3 &position, JarCelestialBody *relative_to,
                                      bool frame_relative) const
{
    if (_bodies.empty())
    {
        const float g = float(setting("physics/3d/default_gravity", 9.8));
        return Vector3(setting("physics/3d/default_gravity_vector", Vector3(0, -1, 0))) * g;
    }
    if (!bool(setting(PhysicalStrength, true)))
    {
        const JarCelestialBody *dominant = get_dominant_body(position);
        return dominant ? dominant->gravity_at(position) : Vector3();
    }
    // Everything's pull, minus what the anchor itself falls with: physics runs
    // in the anchor's frame, where far bodies only add their tides.
    Vector3 sum;
    for (const JarCelestialBody *body : _bodies)
        sum += body->gravity_at(position);
    if (const JarCelestialBody *anchor = !frame_relative ? nullptr : relative_to ? relative_to : get_anchor())
    {
        const Vector3 center = anchor->get_center();
        for (const JarCelestialBody *body : _bodies)
            if (body != anchor)
                sum -= body->gravity_at(center);
    }
    return sum;
}

Vector3 JarVoxelUniverse::get_up(const Vector3 &position, const Vector3 &fallback, JarCelestialBody *relative_to,
                                 bool frame_relative) const
{
    const double sharpness = double(setting(UpSharpness, 4.0));
    // The same pulls as get_gravity(): in the anchor's frame, other bodies
    // only add their tides (a planet does not tilt the ground of its moon).
    const JarCelestialBody *anchor =
        bool(setting(PhysicalStrength, true)) && frame_relative ? (relative_to ? relative_to : get_anchor()) : nullptr;
    const Vector3 anchorCenter = anchor ? anchor->get_center() : Vector3();
    Vector3 pull;
    for (const JarCelestialBody *body : _bodies)
    {
        Vector3 g = body->gravity_at(position);
        if (anchor && body != anchor)
            g -= body->gravity_at(anchorCenter);
        const real_t strength = g.length();
        if (strength > 1e-6f)
            pull += g / strength * real_t(std::pow(double(strength), sharpness));
    }
    return pull.length_squared() > 1e-20f ? -pull.normalized() : fallback;
}

JarCelestialBody *JarVoxelUniverse::get_dominant_body(const Vector3 &position) const
{
    JarCelestialBody *best = nullptr;
    real_t bestStrength = -1.0f;
    for (JarCelestialBody *body : _bodies)
    {
        const real_t strength = body->gravity_at(position).length();
        if (strength > bestStrength)
        {
            best = body;
            bestStrength = strength;
        }
    }
    return best;
}

JarCelestialBody *JarVoxelUniverse::get_anchor() const
{
    if (auto *body = Object::cast_to<JarCelestialBody>(ObjectDB::get_instance(_anchorOverride)))
        return body;
    return Object::cast_to<JarCelestialBody>(ObjectDB::get_instance(_anchor));
}

void JarVoxelUniverse::set_anchor_override(JarCelestialBody *body)
{
    _anchorOverride = body ? body->get_instance_id() : 0;
}

JarCelestialBody *JarVoxelUniverse::get_anchor_override() const
{
    return Object::cast_to<JarCelestialBody>(ObjectDB::get_instance(_anchorOverride));
}

void JarVoxelUniverseUpdater::_notification(int what)
{
    if (what == NOTIFICATION_READY)
    {
        // Last in the physics frame: moved bodies' colliders are flushed into place
        // before the physics step, so the next frame sees a consistent world.
        set_physics_process_priority(INT_MAX);
        set_physics_process_internal(true);
    }
    else if (what == NOTIFICATION_INTERNAL_PHYSICS_PROCESS)
    {
        if (JarVoxelUniverse *universe = JarVoxelUniverse::get_singleton())
            universe->step(this);
    }
}
