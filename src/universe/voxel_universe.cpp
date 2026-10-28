#include "voxel_universe.h"
#include "body_orbit.h"
#include "celestial_body.h"
#include "voxel_viewer.h"
#include <algorithm>
#include <climits>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
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
    ClassDB::bind_method(D_METHOD("get_gravity", "global_position"), &JarVoxelUniverse::get_gravity);
    ClassDB::bind_method(D_METHOD("get_up", "global_position", "fallback"), &JarVoxelUniverse::get_up,
                         DEFVAL(Vector3(0, 1, 0)));
    ClassDB::bind_method(D_METHOD("get_dominant_body", "global_position"), &JarVoxelUniverse::get_dominant_body);
    ClassDB::bind_method(D_METHOD("get_anchor"), &JarVoxelUniverse::get_anchor);
    ClassDB::bind_method(D_METHOD("set_anchor_override", "body"), &JarVoxelUniverse::set_anchor_override);
    ClassDB::bind_method(D_METHOD("get_anchor_override"), &JarVoxelUniverse::get_anchor_override);
}

void JarVoxelUniverse::register_body(JarCelestialBody *body)
{
    if (std::find(_bodies.begin(), _bodies.end(), body) == _bodies.end())
        _bodies.push_back(body);
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
    if (_bodies.empty())
        if (auto *updater = Object::cast_to<Node>(ObjectDB::get_instance(_updater)))
        {
            restore_space_gravity(updater);
            updater->queue_free();
            _updater = 0;
        }
}

void JarVoxelUniverse::register_orbit(JarBodyOrbit *orbit)
{
    if (std::find(_orbits.begin(), _orbits.end(), orbit) == _orbits.end())
        _orbits.push_back(orbit);
    // Parents first: a moon's orbit is around where its planet is now.
    std::sort(_orbits.begin(), _orbits.end(), [](const JarBodyOrbit *a, const JarBodyOrbit *b) {
        return depth(a) < depth(b);
    });
}

void JarVoxelUniverse::unregister_orbit(JarBodyOrbit *orbit)
{
    _orbits.erase(std::remove(_orbits.begin(), _orbits.end(), orbit), _orbits.end());
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
    for (JarBodyOrbit *orbit : _orbits)
        orbit->apply(time);
    for (JarCelestialBody *body : _bodies)
        body->apply_spin(time);
    JarCelestialBody *anchor = choose_anchor();
    _anchor = anchor ? anchor->get_instance_id() : 0;
    if (bool(setting(SpaceGravity, true)))
        apply_space_gravity(context, anchor);
}

JarCelestialBody *JarVoxelUniverse::choose_anchor() const
{
    if (auto *body = Object::cast_to<JarCelestialBody>(ObjectDB::get_instance(_anchorOverride)))
        return body;
    // Where the first viewer is (else the camera).
    Vector3 at;
    bool found = false;
    for (const JarVoxelViewer *viewer : JarVoxelViewer::get_viewers())
        if (viewer->is_enabled() && viewer->is_inside_tree())
        {
            at = viewer->get_global_position();
            found = true;
            break;
        }
    if (!found)
    {
        auto *updater = Object::cast_to<Node>(ObjectDB::get_instance(_updater));
        Viewport *viewport = updater && updater->is_inside_tree() ? updater->get_viewport() : nullptr;
        if (Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr)
        {
            at = camera->get_global_position();
            found = true;
        }
    }
    JarCelestialBody *dominant = get_dominant_body(at);
    auto *current = Object::cast_to<JarCelestialBody>(ObjectDB::get_instance(_anchor));
    if (!found || dominant == nullptr || current == nullptr || dominant == current)
        return dominant ? dominant : current;
    // A little hysteresis: no flipping back and forth at the balance point.
    return dominant->gravity_at(at).length() > 1.25f * current->gravity_at(at).length() ? dominant : current;
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

Vector3 JarVoxelUniverse::get_gravity(const Vector3 &position) const
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
    if (const JarCelestialBody *anchor = get_anchor())
    {
        const Vector3 center = anchor->get_center();
        for (const JarCelestialBody *body : _bodies)
            if (body != anchor)
                sum -= body->gravity_at(center);
    }
    return sum;
}

Vector3 JarVoxelUniverse::get_up(const Vector3 &position, const Vector3 &fallback) const
{
    const double sharpness = double(setting(UpSharpness, 4.0));
    // The same pulls as get_gravity(): in the anchor's frame, other bodies
    // only add their tides (a planet does not tilt the ground of its moon).
    const JarCelestialBody *anchor = bool(setting(PhysicalStrength, true)) ? get_anchor() : nullptr;
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
