#include "body_layer.h"
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/world3d.hpp>
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
const char *ExtinctionShader = "res://addons/jar_voxel_terrain/nodes/atmosphere/atmosphere_extinction.gdshader";
// Light a JarBodyLight gives its light before any atmosphere tints it.
const char *BaseColorMeta = "jar_base_color";
// The Environment's own ambient light (starlight), under the sky's.
const char *AmbientFloorMeta = "jar_ambient_floor";

glm::dvec3 to_d(const Vector3 &v)
{
    return glm::dvec3(v.x, v.y, v.z);
}
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
    // A star's light first.
    if (tree != nullptr && !Engine::get_singleton()->is_editor_hint())
    {
        const TypedArray<Node> stars = tree->get_nodes_in_group(JarBodyLight::LightGroup);
        if (!stars.is_empty())
            if (auto *light = Object::cast_to<DirectionalLight3D>(stars[0]))
            {
                _foundSun = light->get_instance_id();
                return light;
            }
    }
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

// ---------------------------------------------------------------- JarBodyLight

void JarBodyLight::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_physical_size"), &JarBodyLight::get_physical_size);
    ClassDB::bind_method(D_METHOD("set_physical_size", "value"), &JarBodyLight::set_physical_size);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "physical_size"), "set_physical_size", "get_physical_size");
    ClassDB::bind_method(D_METHOD("get_direction_at", "global_position"), &JarBodyLight::get_direction_at);
    ClassDB::bind_method(D_METHOD("get_angular_diameter_at", "global_position"),
                         &JarBodyLight::get_angular_diameter_at);
}

Vector3 JarBodyLight::get_direction_at(const Vector3 &global_position) const
{
    const JarCelestialBody *body = get_body();
    if (body == nullptr)
        return Vector3(0, 1, 0);
    const Vector3 d = body->get_center() - global_position;
    return d.length_squared() > 1e-12f ? d.normalized() : Vector3(0, 1, 0);
}

float JarBodyLight::get_angular_diameter_at(const Vector3 &global_position) const
{
    const JarCelestialBody *body = get_body();
    if (body == nullptr)
        return 0.0f;
    const float distance = (body->get_center() - global_position).length();
    const float radius = body->get_effective_radius();
    if (distance <= radius)
        return 180.0f;
    return float(Math::rad_to_deg(2.0 * std::asin(double(radius) / double(distance))));
}

void JarBodyLight::update(const JarCelestialBody &body)
{
    DirectionalLight3D *light = find_sun();
    if (light == nullptr)
        return;
    if (!light->is_in_group(LightGroup))
        light->add_to_group(LightGroup);
    // From the star towards the camera (where the light matters; the scene's
    // origin in the editor); the light shines along its -z.
    Viewport *viewport = is_inside_tree() ? get_viewport() : nullptr;
    Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
    const Vector3 at = camera ? camera->get_global_position() : Vector3();
    const Vector3 towards = get_direction_at(at);
    const Vector3 helper = std::abs(towards.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
    const Vector3 x = helper.cross(towards).normalized();
    const Basis basis(x, towards.cross(x), towards);
    if (!light->get_global_basis().is_equal_approx(basis))
        light->set_global_basis(basis);
    if (_physicalSize)
        light->set_param(Light3D::PARAM_SIZE, std::min(get_angular_diameter_at(at), 90.0f));
    // Through the air the camera is in: reddened towards the horizon, none
    // below it. The light's own color is kept as its color above the air (at
    // runtime only: in the editor it would be saved with the scene).
    if (Engine::get_singleton()->is_editor_hint())
        return;
    if (!light->has_meta(BaseColorMeta))
        light->set_meta(BaseColorMeta, light->get_color());
    const Color base = light->get_meta(BaseColorMeta);
    Color tint(1, 1, 1);
    if (SceneTree *tree = get_tree())
    {
        const TypedArray<Node> atmospheres = tree->get_nodes_in_group(JarBodyAtmosphere::Group);
        for (int64_t i = 0; i < atmospheres.size(); ++i)
            if (const auto *atmosphere = Object::cast_to<JarBodyAtmosphere>(atmospheres[i]);
                atmosphere && atmosphere->contains(at))
            {
                // Balanced so the sun overhead keeps the light's color; only a
                // lower sun reddens.
                const Color sunlight = atmosphere->get_sunlight_at(at, towards);
                const Color noon = atmosphere->get_noon_sunlight();
                tint *= Color(std::min(1.0f, sunlight.r / std::max(noon.r, 1e-4f)),
                              std::min(1.0f, sunlight.g / std::max(noon.g, 1e-4f)),
                              std::min(1.0f, sunlight.b / std::max(noon.b, 1e-4f)));
            }
    }
    const Color color(base.r * tint.r, base.g * tint.g, base.b * tint.b);
    if (!light->get_color().is_equal_approx(color))
        light->set_color(color);
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
#define BIND_AIR(m_name, m_type, m_hint, m_hint_text)                                                                 \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarBodyAtmosphere::get_##m_name);                                  \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarBodyAtmosphere::set_##m_name);                         \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
    BIND_AIR(thickness, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.01,1,0.01");
    ADD_GROUP("Air", "");
    BIND_AIR(rayleigh_depth, Variant::VECTOR3, PROPERTY_HINT_NONE, "");
    BIND_AIR(rayleigh_height, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.01,2,0.01");
    BIND_AIR(mie_depth, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.001,or_greater");
    BIND_AIR(mie_height, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.01,2,0.01");
    BIND_AIR(mie_anisotropy, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,0.99,0.01");
    BIND_AIR(ozone_depth, Variant::VECTOR3, PROPERTY_HINT_NONE, "");
    BIND_AIR(ozone_height, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
    BIND_AIR(ozone_width, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.01,1,0.01");
    BIND_AIR(ground_albedo, Variant::COLOR, PROPERTY_HINT_COLOR_NO_ALPHA, "");
    ADD_GROUP("Lighting", "");
    BIND_AIR(sunset_strength, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,10,0.01,or_greater");
    BIND_AIR(ambient_strength, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,4,0.01,or_greater");
    ADD_GROUP("", "");
#undef BIND_AIR
    ClassDB::bind_method(D_METHOD("get_sunlight_at", "global_position", "direction"),
                         &JarBodyAtmosphere::get_sunlight_at);
    ClassDB::bind_method(D_METHOD("contains", "global_position"), &JarBodyAtmosphere::contains);
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
    if (_extinction.is_valid())
        _extinction->set_next_pass(get_active_material());
}

void JarBodyAtmosphere::_notification(int what)
{
    if (what == NOTIFICATION_ENTER_TREE)
        add_to_group(Group);
    else if (what == NOTIFICATION_EXIT_TREE && _ownsAmbient && is_inside_tree())
    {
        // Leave the Environment as it was.
        if (const Ref<World3D> world = get_viewport()->find_world_3d(); world.is_valid())
            if (const Ref<Environment> environment = world->get_environment();
                environment.is_valid() && environment->has_meta(AmbientFloorMeta))
            {
                const Color floor = environment->get_meta(AmbientFloorMeta);
                environment->set_ambient_light_color(floor);
                environment->set_ambient_light_energy(1.0f);
            }
        _ownsAmbient = false;
    }
}

void JarBodyAtmosphere::build()
{
    _shell = add_shell();
    _extinction = make_material(ExtinctionShader, 99); // dims what is behind, before the light is added
    _extinction->set_next_pass(get_active_material());
    _shell->set_material_override(_extinction);
}

AtmosphereParams JarBodyAtmosphere::params_for(const JarCelestialBody::Frame &frame) const
{
    AtmosphereParams p;
    p.bottom = frame.seaLevel;
    p.top = frame.seaLevel * (1.0 + _thickness);
    p.rayleighDepth = to_d(_rayleighDepth);
    p.rayleighHeight = _rayleighHeight;
    p.mieDepth = _mieDepth;
    p.mieHeight = _mieHeight;
    p.mieAnisotropy = _mieAnisotropy;
    p.ozoneDepth = to_d(_ozoneDepth);
    p.ozoneCenter = _ozoneHeight;
    p.ozoneWidth = _ozoneWidth;
    p.groundAlbedo = glm::dvec3(_groundAlbedo.r, _groundAlbedo.g, _groundAlbedo.b);
    p.sunsetStrength = _sunsetStrength;
    return p;
}

void JarBodyAtmosphere::apply_air(const Ref<ShaderMaterial> &material, const JarCelestialBody::Frame &frame) const
{
    const AtmosphereParams &p = _model.params();
    const double thickness = p.top - p.bottom;
    // The model's per-meter coefficients at sea level.
    const AtmosphereModel::Medium sea = _model.medium(0.0);
    material->set_shader_parameter("planet_center", frame.center);
    material->set_shader_parameter("planet_radius", float(p.bottom));
    material->set_shader_parameter("atmosphere_radius", float(p.top));
    material->set_shader_parameter("rayleigh_scattering", DTransform::to(sea.rayleighScattering));
    material->set_shader_parameter("rayleigh_scale_height", float(p.rayleighHeight * thickness));
    material->set_shader_parameter("mie_scattering", float(sea.mieScattering.x));
    material->set_shader_parameter("mie_scale_height", float(p.mieHeight * thickness));
    material->set_shader_parameter("mie_absorption", float(p.mieAbsorption));
    material->set_shader_parameter("mie_anisotropy", float(p.mieAnisotropy));
    material->set_shader_parameter("ozone_absorption",
                                   DTransform::to(p.ozoneDepth / std::max(0.5 * p.ozoneWidth * thickness, 1e-6)));
    material->set_shader_parameter("ozone_center", float(p.ozoneCenter * thickness));
    material->set_shader_parameter("ozone_half_width", float(0.5 * p.ozoneWidth * thickness));
    material->set_shader_parameter("transmittance_lut", _transmittanceLut);
    material->set_shader_parameter("multiscattering_lut", _multiScatteringLut);
}

bool JarBodyAtmosphere::contains(const Vector3 &global_position) const
{
    const JarCelestialBody *body = get_body();
    if (body == nullptr || !_model.is_built())
        return false;
    return (global_position - body->get_center()).length() < _model.params().top;
}

Color JarBodyAtmosphere::get_sunlight_at(const Vector3 &global_position, const Vector3 &direction) const
{
    const JarCelestialBody *body = get_body();
    if (body == nullptr || !_model.is_built())
        return Color(1, 1, 1);
    const Vector3 offset = global_position - body->get_center();
    const double r = offset.length();
    if (r >= _model.params().top)
        return Color(1, 1, 1); // above the air (what it hides from space is not tinted)
    const double mu = r > 1e-6 ? offset.dot(direction.normalized()) / r : 1.0;
    const glm::dvec3 t = _model.sunlight(r, mu);
    return Color(t.x, t.y, t.z);
}

Color JarBodyAtmosphere::get_noon_sunlight() const
{
    if (!_model.is_built())
        return Color(1, 1, 1);
    const glm::dvec3 t = _model.sunlight(_model.params().bottom, 1.0);
    return Color(t.x, t.y, t.z);
}

void JarBodyAtmosphere::apply_ambient(const JarCelestialBody &body)
{
    Viewport *viewport = get_viewport();
    Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
    const Ref<World3D> world = viewport ? viewport->find_world_3d() : Ref<World3D>();
    const Ref<Environment> environment = world.is_valid() ? world->get_environment() : Ref<Environment>();
    if (camera == nullptr || environment.is_null() || Engine::get_singleton()->is_editor_hint())
        return;
    if (!environment->has_meta(AmbientFloorMeta))
        environment->set_meta(AmbientFloorMeta,
                              environment->get_ambient_light_color() * environment->get_ambient_light_energy());
    const Color floor = environment->get_meta(AmbientFloorMeta);
    const Vector3 at = camera->get_global_position();
    if (!contains(at))
    {
        if (_ownsAmbient)
        {
            environment->set_ambient_light_color(floor);
            environment->set_ambient_light_energy(1.0f);
            _ownsAmbient = false;
        }
        return;
    }
    _ownsAmbient = true;
    // The sky's light: its average radiance over the upper hemisphere, for
    // the sunlight above the atmosphere (base color times energy).
    Color sky(0, 0, 0);
    if (DirectionalLight3D *light = find_sun())
    {
        const Vector3 offset = at - body.get_center();
        const double r = offset.length();
        const Vector3 towards = light->get_global_basis().get_column(2).normalized();
        const glm::dvec3 radiance = _model.sky_ambient(r, offset.dot(towards) / std::max(r, 1e-6));
        const Color base = light->has_meta(BaseColorMeta) ? Color(light->get_meta(BaseColorMeta)) : light->get_color();
        // As bright as the sky is drawn (the material's sunlight intensity), so
        // shadows are lit by the sky one sees.
        const Variant intensity = get_active_material()->get_shader_parameter("sun_intensity");
        const float scale = (intensity.get_type() == Variant::NIL ? 16.0f : float(intensity)) * _ambientStrength;
        sky = Color(radiance.x, radiance.y, radiance.z) * base * scale;
    }
    const Color total(floor.r + sky.r, floor.g + sky.g, floor.b + sky.b);
    const float energy = std::max({total.r, total.g, total.b, 1e-6f});
    environment->set_ambient_light_color(Color(total.r / energy, total.g / energy, total.b / energy));
    environment->set_ambient_light_energy(energy);
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
    // The tables, when the air or the planet changed (a few milliseconds).
    const AtmosphereParams params = params_for(frame);
    if (!_model.is_built() || !(params == _model.params()))
    {
        _model.build(params);
        _transmittanceLut = ImageTexture::create_from_image(_model.transmittance_image());
        _multiScatteringLut = ImageTexture::create_from_image(_model.multiple_scattering_image());
    }
    const Ref<ShaderMaterial> material = get_active_material();
    if (_extinction->get_next_pass() != material)
        _extinction->set_next_pass(material);
    for (const Ref<ShaderMaterial> &m : {material, _extinction})
    {
        apply_air(m, frame);
        apply_sun(m);
        // The light above the atmosphere: it reddens on the way down here.
        if (DirectionalLight3D *light = find_sun(); light && light->has_meta(BaseColorMeta))
        {
            const Color base = light->get_meta(BaseColorMeta);
            m->set_shader_parameter("sun_color", Vector3(base.r, base.g, base.b));
        }
    }
    apply_ambient(body);
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
