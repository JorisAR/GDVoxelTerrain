#include "body_orbit.h"
#include "celestial_body.h"
#include "voxel_universe.h"
#include <cmath>
#include <godot_cpp/classes/engine.hpp>

void JarBodyOrbit::_bind_methods()
{
#define BIND_ORBIT(m_name, m_type, m_hint, m_hint_text)                                                               \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarBodyOrbit::get_##m_name);                                       \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarBodyOrbit::set_##m_name);                              \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
    BIND_ORBIT(radius, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,10000000,1,or_greater,suffix:m");
    BIND_ORBIT(period, Variant::FLOAT, PROPERTY_HINT_NONE, "suffix:s");
    BIND_ORBIT(axis, Variant::VECTOR3, PROPERTY_HINT_NONE, "");
    BIND_ORBIT(phase, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,360,0.1,suffix:deg");
#undef BIND_ORBIT
    ClassDB::bind_method(D_METHOD("get_parent_body"), &JarBodyOrbit::get_parent_body);
    ClassDB::bind_method(D_METHOD("offset_at", "time"), &JarBodyOrbit::offset_at);
}

void JarBodyOrbit::_notification(int what)
{
    const bool editor = Engine::get_singleton()->is_editor_hint();
    switch (what)
    {
    case NOTIFICATION_ENTER_TREE:
        set_as_top_level(true); // the parent's spin must not turn the orbit
        if (editor)
            set_process_internal(true); // preview the position at time 0
        else if (JarVoxelUniverse *universe = JarVoxelUniverse::get_singleton())
        {
            universe->register_orbit(this);
            _registered = true;
        }
        break;
    case NOTIFICATION_EXIT_TREE:
        if (_registered)
            if (JarVoxelUniverse *universe = JarVoxelUniverse::get_singleton())
                universe->unregister_orbit(this);
        _registered = false;
        break;
    case NOTIFICATION_INTERNAL_PROCESS:
        apply(0.0);
        break;
    default:
        break;
    }
}

JarCelestialBody *JarBodyOrbit::get_parent_body() const
{
    for (Node *n = get_parent(); n != nullptr; n = n->get_parent())
        if (JarCelestialBody *body = Object::cast_to<JarCelestialBody>(n))
            return body;
    return nullptr;
}

glm::dvec3 JarBodyOrbit::offset(double time) const
{
    const glm::dvec3 normal = _axis.length_squared() > 1e-12f ? glm::normalize(DTransform::from(_axis)) : glm::dvec3(0, 1, 0);
    // Angle 0 lies along +x (or +z when the orbit is about x), in the orbit plane.
    glm::dvec3 start = glm::dvec3(1, 0, 0) - normal * normal.x;
    if (glm::dot(start, start) < 1e-6)
        start = glm::dvec3(0, 0, 1) - normal * normal.z;
    start = glm::normalize(start);
    double turns = double(_phase) / 360.0;
    if (_period != 0.0f)
        turns += time / double(_period);
    return DTransform::rotation(normal, std::fmod(turns, 1.0) * 2.0 * Math_PI) * start * double(_radius);
}

void JarBodyOrbit::apply(double time)
{
    if (!is_inside_tree())
        return;
    const JarCelestialBody *body = get_parent_body();
    const Vector3 center = body ? body->get_center()
                                : (Object::cast_to<Node3D>(get_parent()) ? Object::cast_to<Node3D>(get_parent())->get_global_position()
                                                                         : Vector3());
    set_global_transform(Transform3D(Basis(), center + offset_at(time)));
}
