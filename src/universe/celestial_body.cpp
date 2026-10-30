#include "celestial_body.h"
#include "body_layer.h"
#include "body_orbit.h"
#include "voxel_universe.h"
#include "voxel_volume.h"
#include <cmath>
#include <godot_cpp/classes/engine.hpp>

void JarCelestialBody::_bind_methods()
{
    BIND_ENUM_CONSTANT(SHAPE_SPHERE);
    BIND_ENUM_CONSTANT(SHAPE_PLANE);
#define BIND_BODY(m_name, m_type, m_hint, m_hint_text)                                                                \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarCelestialBody::get_##m_name);                                   \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarCelestialBody::set_##m_name);                          \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
    BIND_BODY(volume, Variant::OBJECT, PROPERTY_HINT_NODE_TYPE, "JarVoxelVolume");
    ADD_GROUP("Without a Volume", "");
    BIND_BODY(shape, Variant::INT, PROPERTY_HINT_ENUM, "Sphere,Plane");
    BIND_BODY(radius, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1000000,1,or_greater,suffix:m");
    BIND_BODY(sea_level, Variant::FLOAT, PROPERTY_HINT_NONE, "suffix:m");
    ADD_GROUP("Gravity", "");
    BIND_BODY(surface_gravity, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,suffix:m/s2");
    BIND_BODY(gravity_reach, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1000000,1,or_greater,suffix:m");
    ADD_GROUP("Spin", "spin_");
    BIND_BODY(spin_period, Variant::FLOAT, PROPERTY_HINT_NONE, "suffix:s");
    BIND_BODY(spin_axis, Variant::VECTOR3, PROPERTY_HINT_NONE, "");
    ADD_GROUP("Frame", "");
    BIND_BODY(rotating_frame_height, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,100000,1,or_greater,suffix:m");
#undef BIND_BODY
    ClassDB::bind_method(D_METHOD("get_terrain"), &JarCelestialBody::get_terrain);
    ClassDB::bind_method(D_METHOD("get_center"), &JarCelestialBody::get_center);
    ClassDB::bind_method(D_METHOD("get_up_at", "global_position"), &JarCelestialBody::get_up_at);
    ClassDB::bind_method(D_METHOD("get_gravity_at", "global_position"), &JarCelestialBody::gravity_at);
    ClassDB::bind_method(D_METHOD("get_effective_radius"), &JarCelestialBody::get_effective_radius);
    ClassDB::bind_method(D_METHOD("get_effective_sea_level"), &JarCelestialBody::get_effective_sea_level);
    ClassDB::bind_method(D_METHOD("get_effective_rotating_frame_height"),
                         &JarCelestialBody::get_effective_rotating_frame_height);
    ClassDB::bind_method(D_METHOD("get_altitude", "global_position"), &JarCelestialBody::get_altitude);
    ClassDB::bind_method(D_METHOD("get_sphere_of_influence"), &JarCelestialBody::get_sphere_of_influence);
}

void JarCelestialBody::_notification(int what)
{
    if (Engine::get_singleton()->is_editor_hint())
        return;
    if (what == NOTIFICATION_ENTER_TREE)
    {
        if (JarVoxelUniverse *universe = JarVoxelUniverse::get_singleton())
        {
            universe->register_body(this);
            _registered = true;
        }
    }
    else if (what == NOTIFICATION_EXIT_TREE && _registered)
    {
        if (JarVoxelUniverse *universe = JarVoxelUniverse::get_singleton())
            universe->unregister_body(this);
        _registered = false;
    }
}

JarVoxelVolume *JarCelestialBody::get_terrain() const
{
    if (_volume)
        return _volume;
    for (int i = 0; i < get_child_count(); ++i)
        if (JarVoxelVolume *volume = Object::cast_to<JarVoxelVolume>(get_child(i)))
            return volume;
    return nullptr;
}

JarCelestialBody::Frame JarCelestialBody::get_frame() const
{
    Frame frame;
    if (const JarVoxelVolume *volume = get_terrain(); volume && volume->is_inside_tree())
    {
        const UpField field = volume->get_up_field();
        const Transform3D toWorld = volume->get_global_transform();
        const real_t scale = toWorld.basis.get_scale().x;
        frame.radial = field.mode == UpField::RADIAL;
        frame.center = toWorld.xform(Vector3(field.center.x, field.center.y, field.center.z));
        frame.axis = toWorld.basis.xform(Vector3(field.axis.x, field.axis.y, field.axis.z)).normalized();
        frame.seaLevel = field.sea_level * scale;
        frame.radius = _radius > 0.0f ? _radius : frame.seaLevel;
    }
    else
    {
        const Transform3D toWorld = is_inside_tree() ? get_global_transform() : get_transform();
        frame.radial = _shape == SHAPE_SPHERE;
        frame.center = toWorld.origin;
        frame.axis = toWorld.basis.get_column(1).normalized();
        frame.radius = _radius;
        frame.seaLevel = frame.radial ? _radius + _seaLevel : _seaLevel;
    }
    return frame;
}

float JarCelestialBody::get_effective_rotating_frame_height() const
{
    if (_rotatingFrameHeight > 0.0f)
        return _rotatingFrameHeight;
    const Frame frame = get_frame();
    if (!frame.radial)
        return INFINITY;
    for (int i = 0; i < get_child_count(); ++i)
        if (const auto *atmosphere = Object::cast_to<JarBodyAtmosphere>(get_child(i)))
            return atmosphere->get_thickness() * frame.seaLevel;
    return 0.5f * frame.radius;
}

float JarCelestialBody::get_altitude(const Vector3 &position) const
{
    const Frame frame = get_frame();
    if (!frame.radial)
        return (position - frame.center).dot(frame.axis) - frame.seaLevel;
    return (position - frame.center).length() - frame.seaLevel;
}

double JarCelestialBody::get_mass() const
{
    const Frame frame = get_frame();
    return frame.radial ? double(_surfaceGravity) * double(frame.radius) * double(frame.radius) : INFINITY;
}

float JarCelestialBody::get_sphere_of_influence() const
{
    const auto *orbit = Object::cast_to<JarBodyOrbit>(_carrier);
    const JarCelestialBody *parent = orbit ? orbit->get_parent_body() : nullptr;
    if (parent == nullptr)
        return INFINITY;
    const double ratio = get_mass() / parent->get_mass();
    return float(double(orbit->get_radius()) * std::pow(std::min(ratio, 1.0), 0.4));
}

Vector3 JarCelestialBody::get_local_center() const
{
    const JarVoxelVolume *volume = get_terrain();
    if (volume == nullptr || !volume->is_inside_tree())
        return Vector3();
    const UpField field = volume->get_up_field();
    Vector3 p(field.center.x, field.center.y, field.center.z);
    for (const Node *n = volume; n != nullptr && n != this; n = n->get_parent())
        if (const Node3D *n3 = Object::cast_to<Node3D>(n))
            p = n3->get_transform().xform(p);
    return p;
}

Vector3 JarCelestialBody::gravity_at(const Vector3 &position) const
{
    if (_surfaceGravity <= 0.0f)
        return Vector3();
    const Frame frame = get_frame();
    if (!frame.radial)
        return -frame.axis * _surfaceGravity;
    const Vector3 offset = position - frame.center;
    const real_t r = offset.length();
    if (r < 1e-6f)
        return Vector3();
    const real_t radius = std::max<real_t>(frame.radius, 1.0f);
    // Inverse square outside; inside, falling to zero at the center (a solid
    // ball), so it stays finite when digging deep.
    real_t strength = r >= radius ? _surfaceGravity * (radius / r) * (radius / r) : _surfaceGravity * (r / radius);
    if (_gravityReach > 0.0f)
    {
        // Fades out over the last tenth of the reach.
        const real_t t = std::clamp<real_t>((_gravityReach - r) / (0.1f * _gravityReach), 0.0f, 1.0f);
        strength *= t * t * (3.0f - 2.0f * t);
    }
    return -offset / r * strength;
}

glm::dmat3 JarCelestialBody::spin_at(double time) const
{
    if (_spinPeriod == 0.0f || _spinAxis.length_squared() < 1e-12f)
        return glm::dmat3(1.0);
    // Wrapped in doubles: the angle stays exact however long the game runs.
    const double turns = std::fmod(time / double(_spinPeriod), 1.0);
    return DTransform::rotation(glm::normalize(DTransform::from(_spinAxis)), turns * 2.0 * Math_PI);
}
