#include "body_layer.h"
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/plane_mesh.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/viewport.hpp>

namespace
{
const char *WaterShader = "res://addons/jar_voxel_terrain/nodes/water/water.gdshader";
const char *UnderwaterShader = "res://addons/jar_voxel_terrain/nodes/water/underwater.gdshader";
const char *AtmosphereShader = "res://addons/jar_voxel_terrain/nodes/atmosphere/atmosphere.gdshader";
const char *CloudShader = "res://addons/jar_voxel_terrain/nodes/clouds/clouds.gdshader";

// A frame with `up` as its y axis.
Basis basis_with_up(const Vector3 &up)
{
    const Vector3 x = up.cross(std::abs(up.x) < 0.9f ? Vector3(1, 0, 0) : Vector3(0, 0, -1)).normalized();
    return Basis(x, up, x.cross(up).normalized());
}

Ref<SphereMesh> make_sphere(float radius, int segments, int rings)
{
    Ref<SphereMesh> sphere;
    sphere.instantiate();
    sphere->set_radius(radius);
    sphere->set_height(radius * 2.0f);
    sphere->set_radial_segments(segments);
    sphere->set_rings(rings);
    return sphere;
}

Ref<PlaneMesh> make_plane(float size, int subdivisions)
{
    Ref<PlaneMesh> plane;
    plane.instantiate();
    plane->set_size(Vector2(size, size));
    plane->set_subdivide_width(subdivisions);
    plane->set_subdivide_depth(subdivisions);
    return plane;
}
} // namespace

// ---------------------------------------------------------------- JarBodyLayer

void JarBodyLayer::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_body"), &JarBodyLayer::get_body);
    ClassDB::bind_method(D_METHOD("find_sun"), &JarBodyLayer::find_sun);
    ClassDB::bind_method(D_METHOD("get_sun"), &JarBodyLayer::get_sun);
    ClassDB::bind_method(D_METHOD("set_sun", "light"), &JarBodyLayer::set_sun);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sun", PROPERTY_HINT_NODE_TYPE, "DirectionalLight3D"), "set_sun",
                 "get_sun");
}

void JarBodyLayer::_notification(int what)
{
    if (what == NOTIFICATION_READY)
    {
        build();
        set_process_internal(true);
    }
    else if (what == NOTIFICATION_INTERNAL_PROCESS)
    {
        if (const JarCelestialBody *body = get_body())
            update(*body);
    }
}

JarCelestialBody *JarBodyLayer::get_body() const
{
    for (Node *n = get_parent(); n != nullptr; n = n->get_parent())
        if (JarCelestialBody *body = Object::cast_to<JarCelestialBody>(n))
            return body;
    return nullptr;
}

DirectionalLight3D *JarBodyLayer::find_sun()
{
    if (_sun)
        return _sun;
    // Searching the tree is slow (chunks and features are many nodes): only
    // when the previous one is gone.
    if (auto *light = Object::cast_to<DirectionalLight3D>(ObjectDB::get_instance(_foundSun)))
        if (light->is_inside_tree())
            return light;
    _foundSun = 0;
    SceneTree *tree = get_tree();
    Node *root = tree == nullptr ? nullptr
                 : Engine::get_singleton()->is_editor_hint() ? tree->get_edited_scene_root()
                                                             : tree->get_current_scene();
    if (root == nullptr)
        return nullptr;
    const TypedArray<Node> lights = root->find_children("*", "DirectionalLight3D", true, false);
    if (lights.is_empty())
        return nullptr;
    auto *light = Object::cast_to<DirectionalLight3D>(lights[0]);
    _foundSun = light ? light->get_instance_id() : 0;
    return light;
}

MeshInstance3D *JarBodyLayer::add_shell()
{
    MeshInstance3D *mesh = memnew(MeshInstance3D);
    mesh->set_as_top_level(true);
    mesh->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
    add_child(mesh, false, INTERNAL_MODE_FRONT);
    return mesh;
}

Ref<ShaderMaterial> JarBodyLayer::make_material(const String &shaderPath, int priority)
{
    Ref<ShaderMaterial> material;
    material.instantiate();
    material->set_shader(ResourceLoader::get_singleton()->load(shaderPath));
    material->set_render_priority(priority);
    return material;
}

Vector3 JarBodyLayer::eye(const JarCelestialBody::Frame &frame) const
{
    Viewport *viewport = is_inside_tree() ? get_viewport() : nullptr;
    if (Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr)
        return camera->get_global_position();
    return frame.center + frame.axis * (frame.seaLevel + 100.0f);
}

void JarBodyLayer::apply_sun(const Ref<ShaderMaterial> &material)
{
    if (DirectionalLight3D *light = find_sun())
    {
        material->set_shader_parameter("sun_direction", light->get_global_basis().get_column(2).normalized());
        const Color c = light->get_color();
        material->set_shader_parameter("sun_color", Vector3(c.r, c.g, c.b));
    }
}

// ---------------------------------------------------------------- JarBodyWater

void JarBodyWater::_bind_methods()
{
#define BIND_WATER(m_name, m_type, m_hint, m_hint_text)                                                               \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarBodyWater::get_##m_name);                                       \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarBodyWater::set_##m_name);                              \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
    BIND_WATER(level_offset, Variant::FLOAT, PROPERTY_HINT_NONE, "suffix:m");
    BIND_WATER(material, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial");
    BIND_WATER(patch_size, Variant::FLOAT, PROPERTY_HINT_RANGE, "10,10000,1,suffix:m");
    BIND_WATER(patch_resolution, Variant::INT, PROPERTY_HINT_RANGE, "16,512,1");
    BIND_WATER(underwater_material, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial");
#undef BIND_WATER
    ClassDB::bind_method(D_METHOD("get_active_material"), &JarBodyWater::get_active_material);
    ClassDB::bind_method(D_METHOD("get_active_underwater_material"), &JarBodyWater::get_active_underwater_material);
}

Ref<ShaderMaterial> JarBodyWater::get_active_material()
{
    if (_material.is_valid())
        return _material;
    if (_defaultMaterial.is_null())
        _defaultMaterial = make_material(WaterShader, 0);
    return _defaultMaterial;
}

Ref<ShaderMaterial> JarBodyWater::get_active_underwater_material()
{
    if (_underwaterMaterial.is_valid())
        return _underwaterMaterial;
    if (_defaultUnderwater.is_null())
        _defaultUnderwater = make_material(UnderwaterShader, 10); // after the water surface, before clouds and air
    return _defaultUnderwater;
}

void JarBodyWater::set_material(const Ref<ShaderMaterial> &value)
{
    _material = value;
    apply_materials();
}

void JarBodyWater::set_underwater_material(const Ref<ShaderMaterial> &value)
{
    _underwaterMaterial = value;
    apply_materials();
}

void JarBodyWater::set_patch_size(float value)
{
    _patchSize = std::max(10.0f, value);
    if (_near)
        _near->set_mesh(make_plane(_patchSize, _patchResolution));
}

void JarBodyWater::set_patch_resolution(int value)
{
    _patchResolution = std::clamp(value, 16, 512);
    if (_near)
        _near->set_mesh(make_plane(_patchSize, _patchResolution));
}

void JarBodyWater::apply_materials()
{
    if (_far == nullptr)
        return;
    _far->set_material_override(get_active_material());
    _near->set_material_override(get_active_material());
    _fog->set_material_override(get_active_underwater_material());
}

void JarBodyWater::build()
{
    // A small sphere around the camera: the fog shader covers the screen from it.
    _fog = add_shell();
    _fog->set_mesh(make_sphere(2.0f, 16, 8));
    _fog->set_extra_cull_margin(16384.0f);
    _far = add_shell();
    _near = add_shell();
    _near->set_mesh(make_plane(_patchSize, _patchResolution));
    apply_materials();
}

void JarBodyWater::set_hole(const Vector3 &at, float radius)
{
    _far->set_instance_shader_parameter("hole_center", at);
    _far->set_instance_shader_parameter("hole_radius", radius);
}

void JarBodyWater::update(const JarCelestialBody &body)
{
    if (_far == nullptr)
        return;
    const JarCelestialBody::Frame frame = body.get_frame();
    const float level = frame.seaLevel + _levelOffset;
    for (const Ref<ShaderMaterial> &material : {get_active_material(), get_active_underwater_material()})
    {
        material->set_shader_parameter("water_center", frame.center);
        material->set_shader_parameter("water_axis", frame.axis);
        material->set_shader_parameter("water_level", level);
        material->set_shader_parameter("water_radial", frame.radial);
        // Waves and foam move with the body when it turns in the scene.
        material->set_shader_parameter("world_to_body", body.get_global_basis().orthonormalized().transposed());
    }
    const Vector3 at = eye(frame);
    _fog->set_global_position(at);
    if (frame.radial)
    {
        if (!Math::is_equal_approx(_sphereRadius, level))
        {
            _far->set_mesh(make_sphere(level, 192, 96));
            _sphereRadius = level;
        }
        _far->set_global_transform(Transform3D(Basis(), frame.center));
        // The patch lies on the tangent plane under the camera; the shader
        // bends it onto the sphere. It sits on top of the coarse sphere, whose
        // flat facets lie slightly inside the sphere: no z-fighting.
        const Vector3 up = (at - frame.center).normalized();
        _near->set_global_transform(Transform3D(basis_with_up(up), frame.center + up * level));
        const bool close = (at - frame.center).length() - level < _patchSize;
        _near->set_visible(close);
        set_hole(close ? _near->get_global_position() : frame.center, close ? _patchSize * 0.5f : 0.0f);
    }
    else
    {
        if (_sphereRadius != 0.0f)
        {
            _far->set_mesh(make_plane(100000.0f, 64));
            _sphereRadius = 0.0f;
        }
        Vector3 onPlane = at - frame.axis * (at - frame.center).dot(frame.axis);
        const Basis basis = basis_with_up(frame.axis);
        _far->set_global_transform(Transform3D(basis, onPlane - frame.axis * 0.05f));
        // Snapped to the patch grid so the vertices do not swim.
        const float step = _patchSize / float(_patchResolution);
        onPlane = onPlane.snapped(Vector3(step, step, step));
        _near->set_global_transform(Transform3D(basis, onPlane));
        _near->set_visible(true);
        set_hole(onPlane, _patchSize * 0.5f);
    }
}

// ----------------------------------------------------------- JarBodyAtmosphere

void JarBodyAtmosphere::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_thickness"), &JarBodyAtmosphere::get_thickness);
    ClassDB::bind_method(D_METHOD("set_thickness", "value"), &JarBodyAtmosphere::set_thickness);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "thickness", PROPERTY_HINT_RANGE, "0.01,1,0.01"), "set_thickness",
                 "get_thickness");
    ClassDB::bind_method(D_METHOD("get_material"), &JarBodyAtmosphere::get_material);
    ClassDB::bind_method(D_METHOD("set_material", "value"), &JarBodyAtmosphere::set_material);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial"),
                 "set_material", "get_material");
    ClassDB::bind_method(D_METHOD("get_active_material"), &JarBodyAtmosphere::get_active_material);
}

Ref<ShaderMaterial> JarBodyAtmosphere::get_active_material()
{
    if (_material.is_valid())
        return _material;
    if (_defaultMaterial.is_null())
        _defaultMaterial = make_material(AtmosphereShader, 100); // after opaque and transparent (water): covers them
    return _defaultMaterial;
}

void JarBodyAtmosphere::set_material(const Ref<ShaderMaterial> &value)
{
    _material = value;
    if (_shell)
        _shell->set_material_override(get_active_material());
}

void JarBodyAtmosphere::build()
{
    _shell = add_shell();
    _shell->set_material_override(get_active_material());
}

void JarBodyAtmosphere::update(const JarCelestialBody &body)
{
    if (_shell == nullptr)
        return;
    const JarCelestialBody::Frame frame = body.get_frame();
    if (!frame.radial)
    {
        _shell->set_visible(false); // round bodies only
        return;
    }
    _shell->set_visible(true);
    const float radius = frame.seaLevel;
    const float outer = radius * (1.0f + _thickness);
    // A little larger than the atmosphere, so its back faces always cover it.
    if (!Math::is_equal_approx(_shellRadius, outer * 1.02f))
    {
        _shellRadius = outer * 1.02f;
        _shell->set_mesh(make_sphere(_shellRadius, 64, 32));
    }
    _shell->set_global_transform(Transform3D(Basis(), frame.center));
    const Ref<ShaderMaterial> material = get_active_material();
    material->set_shader_parameter("planet_center", frame.center);
    material->set_shader_parameter("planet_radius", radius);
    material->set_shader_parameter("atmosphere_radius", outer);
    apply_sun(material);
}

// --------------------------------------------------------------- JarBodyClouds

void JarBodyClouds::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_altitude"), &JarBodyClouds::get_altitude);
    ClassDB::bind_method(D_METHOD("set_altitude", "value"), &JarBodyClouds::set_altitude);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "altitude", PROPERTY_HINT_NONE, "suffix:m"), "set_altitude",
                 "get_altitude");
    ClassDB::bind_method(D_METHOD("get_material"), &JarBodyClouds::get_material);
    ClassDB::bind_method(D_METHOD("set_material", "value"), &JarBodyClouds::set_material);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial"),
                 "set_material", "get_material");
    ClassDB::bind_method(D_METHOD("get_active_material"), &JarBodyClouds::get_active_material);
}

Ref<ShaderMaterial> JarBodyClouds::get_active_material()
{
    if (_material.is_valid())
        return _material;
    if (_defaultMaterial.is_null())
        _defaultMaterial = make_material(CloudShader, 50); // after water, before the atmosphere (which hazes them)
    return _defaultMaterial;
}

void JarBodyClouds::set_material(const Ref<ShaderMaterial> &value)
{
    _material = value;
    if (_shell)
        _shell->set_material_override(get_active_material());
}

void JarBodyClouds::build()
{
    _shell = add_shell();
    _shell->set_material_override(get_active_material());
}

void JarBodyClouds::update(const JarCelestialBody &body)
{
    if (_shell == nullptr)
        return;
    const JarCelestialBody::Frame frame = body.get_frame();
    const float level = frame.seaLevel + _altitude;
    if (frame.radial)
    {
        if (!Math::is_equal_approx(_shellRadius, level))
        {
            _shellRadius = level;
            _shell->set_mesh(make_sphere(level, 192, 96));
        }
        _shell->set_global_transform(Transform3D(Basis(), frame.center));
    }
    else
    {
        if (_shellRadius != 0.0f)
        {
            _shellRadius = 0.0f;
            _shell->set_mesh(make_plane(20000.0f, 1));
        }
        const Vector3 at = eye(frame);
        const Vector3 onPlane = at - frame.axis * (at - frame.center).dot(frame.axis) + frame.axis * level;
        _shell->set_global_transform(Transform3D(basis_with_up(frame.axis), onPlane));
    }
    const Ref<ShaderMaterial> material = get_active_material();
    material->set_shader_parameter("planet_center", frame.center);
    material->set_shader_parameter("up_axis", frame.axis);
    material->set_shader_parameter("radial", frame.radial);
    // Clouds and wind move with the body when it turns in the scene.
    material->set_shader_parameter("world_to_body", body.get_global_basis().orthonormalized().transposed());
    apply_sun(material);
}
