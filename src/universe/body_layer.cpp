#include "body_layer.h"
#include <godot_cpp/classes/array_mesh.hpp>
#include "voxel_stats.h"
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/camera_attributes_practical.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/sky.hpp>
#include <godot_cpp/classes/world3d.hpp>
#include "voxel_universe.h"
#include <godot_cpp/classes/plane_mesh.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include "cloud_noise.h"
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
const char *BaseSensitivityMeta = "jar_base_min_sensitivity";
// The Environment's own ambient light (starlight), under the sky's.
const char *AmbientFloorMeta = "jar_ambient_floor";
// On a star's light: the JarBodyLight driving it.
const char *StarLayerMeta = "jar_star_layer";
// The light's own shadow opacity, before fading towards the horizon.
const char *BaseShadowMeta = "jar_base_shadow_opacity";
// Lights are re-aimed once their star moved this far (radians, 0.05 degrees).
constexpr float LightStep = 0.000873f;

// A shader parameter on the scene's sky material (if it is a ShaderMaterial).
void set_sky_parameter(Viewport *viewport, const StringName &name, const Variant &value)
{
    const Ref<World3D> world = viewport ? viewport->find_world_3d() : Ref<World3D>();
    const Ref<Environment> environment = world.is_valid() ? world->get_environment() : Ref<Environment>();
    const Ref<Sky> sky = environment.is_valid() ? environment->get_sky() : Ref<Sky>();
    if (const Ref<ShaderMaterial> material = sky.is_valid() ? Ref<ShaderMaterial>(sky->get_material()) : Ref<ShaderMaterial>();
        material.is_valid() && material->get_shader_parameter(name) != value)
        material->set_shader_parameter(name, value);
}

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

// A flat disc around the origin: rings growing by a constant ratio, so its cells
// look about the same size from the center at any distance.
Ref<ArrayMesh> make_disc(float radius)
{
    const int Segments = 192;
    constexpr float First = 1.5f, Growth = 1.08f;
    const int rings = std::max(2, int(std::ceil(std::log(radius / First) / std::log(Growth))));
    PackedVector3Array vertices, normals;
    PackedInt32Array indices;
    vertices.push_back(Vector3());
    normals.push_back(Vector3(0, 1, 0));
    for (int r = 0; r < rings; ++r)
    {
        const float distance = r == rings - 1 ? radius : First * std::pow(Growth, float(r));
        for (int s = 0; s < Segments; ++s)
        {
            const float angle = Math_TAU * float(s) / float(Segments);
            vertices.push_back(Vector3(std::cos(angle) * distance, 0.0f, std::sin(angle) * distance));
            normals.push_back(Vector3(0, 1, 0));
        }
    }
    const auto at = [Segments](int ring, int segment) { return 1 + ring * Segments + (segment % Segments); };
    for (int s = 0; s < Segments; ++s)
    {
        indices.push_back(0);
        indices.push_back(at(0, s));
        indices.push_back(at(0, s + 1));
    }
    for (int r = 0; r + 1 < rings; ++r)
        for (int s = 0; s < Segments; ++s)
        {
            indices.push_back(at(r, s));
            indices.push_back(at(r + 1, s));
            indices.push_back(at(r, s + 1));
            indices.push_back(at(r, s + 1));
            indices.push_back(at(r + 1, s));
            indices.push_back(at(r + 1, s + 1));
        }
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = vertices;
    arrays[Mesh::ARRAY_NORMAL] = normals;
    arrays[Mesh::ARRAY_INDEX] = indices;
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    return mesh;
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

Ref<ShaderMaterial> JarMaterialCopy::of(const Ref<ShaderMaterial> &material)
{
    if (material.is_null())
    {
        copy.unref();
        source = 0;
        return material;
    }
    if (copy.is_null() || source != material->get_instance_id())
    {
        copy = material->duplicate();
        source = material->get_instance_id();
        synced = Engine::get_singleton()->get_process_frames();
        return copy;
    }
    if (Engine::get_singleton()->is_editor_hint() && synced != Engine::get_singleton()->get_process_frames())
    {
        synced = Engine::get_singleton()->get_process_frames();
        if (copy->get_shader() != material->get_shader())
            copy->set_shader(material->get_shader());
        if (copy->get_render_priority() != material->get_render_priority())
            copy->set_render_priority(material->get_render_priority());
        if (copy->get_next_pass() != material->get_next_pass())
            copy->set_next_pass(material->get_next_pass());
        if (const Ref<Shader> shader = material->get_shader(); shader.is_valid())
        {
            const Array uniforms = shader->get_shader_uniform_list(true);
            for (int64_t i = 0; i < uniforms.size(); ++i)
            {
                const Dictionary u = uniforms[i];
                const StringName name = u["name"];
                const Variant value = material->get_shader_parameter(name);
                if (value.get_type() != Variant::NIL && copy->get_shader_parameter(name) != value)
                    copy->set_shader_parameter(name, value);
            }
        }
    }
    return copy;
}

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
        const VoxelStats::Scope stage(VoxelStats::MainLayers);
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
        const TypedArray<Node> stars = tree->get_nodes_in_group(JarBodyLight::StarGroup);
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
        // The exact direction to the star from the camera: the light itself is
        // re-aimed in steps (shadow edges would crawl), and skies and haze
        // following those steps flickered as the planet turned.
        Viewport *viewport = is_inside_tree() ? get_viewport() : nullptr;
        const Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
        material->set_shader_parameter("sun_direction", camera ? star_direction_at(camera->get_global_position())
                                                                : light->get_global_basis().get_column(2).normalized());
        const Color c = light->get_color();
        material->set_shader_parameter("sun_color", Vector3(c.r, c.g, c.b));
    }
    // A moon's light, for shaders that use it (moon_direction, moon_color:
    // color times energy; black without a moon).
    Color moon(0, 0, 0);
    Vector3 moonDirection(0, 1, 0);
    if (SceneTree *tree = get_tree())
    {
        const TypedArray<Node> moons = tree->get_nodes_in_group(JarBodyLight::MoonGroup);
        if (!moons.is_empty())
            if (const auto *light = Object::cast_to<DirectionalLight3D>(moons[0]); light && light->is_visible())
            {
                moon = light->get_color() * light->get_param(Light3D::PARAM_ENERGY);
                moonDirection = light->get_global_basis().get_column(2).normalized();
            }
    }
    material->set_shader_parameter("moon_direction", moonDirection);
    material->set_shader_parameter("moon_color", Vector3(moon.r, moon.g, moon.b));
}

void JarBodyLayer::apply_star(const Ref<ShaderMaterial> &material, const Vector3 &at)
{
    Vector3 direction, color, ambient;
    far_light(this, at, direction, color, ambient);
    material->set_shader_parameter("sun_direction", direction);
    material->set_shader_parameter("sun_color", color);
}

Vector3 JarBodyLayer::star_direction_at(const Vector3 &global_position)
{
    if (SceneTree *tree = get_tree())
    {
        const TypedArray<Node> stars = tree->get_nodes_in_group(JarBodyLight::StarGroup);
        if (!stars.is_empty())
            if (const auto *light = Object::cast_to<Node>(stars[0]); light && light->has_meta(StarLayerMeta))
                if (const auto *layer = Object::cast_to<JarBodyLight>(ObjectDB::get_instance(uint64_t(light->get_meta(StarLayerMeta)))))
                    return layer->get_direction_at(global_position);
    }
    if (DirectionalLight3D *light = find_sun())
        return light->get_global_basis().get_column(2).normalized();
    return Vector3(0, 1, 0);
}

void JarBodyLayer::far_light(Node *context, const Vector3 &at, Vector3 &direction, Vector3 &color, Vector3 &ambient)
{
    direction = Vector3(0, 1, 0);
    color = Vector3(1, 1, 1);
    ambient = Vector3();
    SceneTree *tree = context && context->is_inside_tree() ? context->get_tree() : nullptr;
    if (tree == nullptr)
        return;
    const TypedArray<Node> stars = tree->get_nodes_in_group(JarBodyLight::StarGroup);
    if (!stars.is_empty())
        if (const auto *light = Object::cast_to<DirectionalLight3D>(stars[0]))
        {
            direction = light->get_global_basis().get_column(2).normalized();
            float falloff = 1.0f;
            if (light->has_meta(StarLayerMeta))
                if (const auto *layer = Object::cast_to<JarBodyLight>(ObjectDB::get_instance(uint64_t(light->get_meta(StarLayerMeta)))))
                {
                    direction = layer->get_direction_at(at);
                    falloff = layer->get_falloff_at(at);
                }
            const Color base = light->has_meta(BaseColorMeta) ? Color(light->get_meta(BaseColorMeta)) : light->get_color() ;
            const float energy = light->get_param(Light3D::PARAM_ENERGY);
            color = Vector3(base.r, base.g, base.b) * energy * falloff;
        }
    if (Viewport *viewport = context->get_viewport())
        if (const Ref<World3D> world = viewport->find_world_3d(); world.is_valid() && world->get_environment().is_valid())
        {
            const Ref<Environment> environment = world->get_environment();
            const Color floor = environment->has_meta(AmbientFloorMeta)
                                    ? Color(environment->get_meta(AmbientFloorMeta))
                                    : environment->get_ambient_light_color() * environment->get_ambient_light_energy();
            ambient = Vector3(floor.r, floor.g, floor.b);
        }
}

void JarBodyLayer::apply_far_light(const Ref<ShaderMaterial> &material, const Vector3 &at)
{
    Vector3 direction, color, ambient;
    far_light(this, at, direction, color, ambient);
    material->set_shader_parameter("far_sun_direction", direction);
    material->set_shader_parameter("far_sun_color", color);
    material->set_shader_parameter("far_ambient", ambient);
}

JarBodyLayer::FarView JarBodyLayer::far_view(const JarCelestialBody &body) const
{
    FarView view;
    Viewport *viewport = is_inside_tree() ? get_viewport() : nullptr;
    const Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
    if (const JarVoxelUniverse *universe = JarVoxelUniverse::get_singleton(); universe && camera)
        static_cast<JarFarView &>(view) = universe->get_far_view(&body, camera);
    view.center = view.to_view(body.get_frame().center);
    return view;
}

// ---------------------------------------------------------------- JarBodyLight

void JarBodyLight::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_physical_size"), &JarBodyLight::get_physical_size);
    ClassDB::bind_method(D_METHOD("set_physical_size", "value"), &JarBodyLight::set_physical_size);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "physical_size"), "set_physical_size", "get_physical_size");
    ClassDB::bind_method(D_METHOD("get_reflects"), &JarBodyLight::get_reflects);
    ClassDB::bind_method(D_METHOD("set_reflects", "value"), &JarBodyLight::set_reflects);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "reflects", PROPERTY_HINT_NODE_TYPE, "JarBodyLight"), "set_reflects",
                 "get_reflects");
    ClassDB::bind_method(D_METHOD("get_energy"), &JarBodyLight::get_energy);
    ClassDB::bind_method(D_METHOD("set_energy", "value"), &JarBodyLight::set_energy);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy", PROPERTY_HINT_RANGE, "0,2,0.001,or_greater"), "set_energy",
                 "get_energy");
    ClassDB::bind_method(D_METHOD("get_inverse_square"), &JarBodyLight::get_inverse_square);
    ClassDB::bind_method(D_METHOD("set_inverse_square", "value"), &JarBodyLight::set_inverse_square);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "inverse_square"), "set_inverse_square", "get_inverse_square");
    ClassDB::bind_method(D_METHOD("get_falloff_at", "global_position"), &JarBodyLight::get_falloff_at);
    ClassDB::bind_method(D_METHOD("get_space_min_sensitivity"), &JarBodyLight::get_space_min_sensitivity);
    ClassDB::bind_method(D_METHOD("set_space_min_sensitivity", "value"), &JarBodyLight::set_space_min_sensitivity);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "space_min_sensitivity", PROPERTY_HINT_RANGE, "0,800,1,or_greater"),
                 "set_space_min_sensitivity", "get_space_min_sensitivity");
    ClassDB::bind_method(D_METHOD("get_phase_at", "global_position"), &JarBodyLight::get_phase_at);
    ClassDB::bind_method(D_METHOD("get_direction_at", "global_position"), &JarBodyLight::get_direction_at);
    ClassDB::bind_method(D_METHOD("get_angular_diameter_at", "global_position"),
                         &JarBodyLight::get_angular_diameter_at);
}

float JarBodyLight::get_falloff_at(const Vector3 &global_position) const
{
    const JarCelestialBody *body = get_body();
    Viewport *viewport = is_inside_tree() ? get_viewport() : nullptr;
    const Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
    if (!_inverseSquare || body == nullptr || camera == nullptr)
        return 1.0f;
    const float here = (camera->get_global_position() - body->get_center()).length();
    const float there = (global_position - body->get_center()).length();
    if (there <= 1.0f)
        return 1.0f;
    return std::clamp((here * here) / (there * there), 0.005f, 20.0f);
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

float JarBodyLight::get_phase_at(const Vector3 &global_position) const
{
    const JarCelestialBody *body = get_body();
    const JarCelestialBody *star = _reflects ? _reflects->get_body() : nullptr;
    if (body == nullptr || star == nullptr)
        return 1.0f;
    // A Lambertian sphere: (sin a + (pi - a) cos a) / pi for the angle a
    // between the sun and the viewer, seen from the moon.
    const Vector3 toSun = (star->get_center() - body->get_center()).normalized();
    const Vector3 toViewer = (global_position - body->get_center()).normalized();
    const float a = std::acos(std::clamp(toSun.dot(toViewer), -1.0f, 1.0f));
    return std::max(0.0f, (std::sin(a) + (float(Math_PI) - a) * std::cos(a)) / float(Math_PI));
}

void JarBodyLight::update(const JarCelestialBody &body)
{
    // A moon drives its own light only (else it would take the sun's).
    DirectionalLight3D *light = _reflects ? get_sun() : find_sun();
    if (light == nullptr)
        return;
    if (!light->is_in_group(LightGroup))
    {
        light->add_to_group(LightGroup);
        light->add_to_group(_reflects ? MoonGroup : StarGroup);
        if (!_reflects)
            light->set_meta(StarLayerMeta, get_instance_id());
        if (_reflects)
            light->set_sky_mode(DirectionalLight3D::SKY_MODE_LIGHT_ONLY); // the moon is drawn itself
    }
    // From the star towards the camera (where the light matters; the scene's
    // origin in the editor); the light shines along its -z.
    Viewport *viewport = is_inside_tree() ? get_viewport() : nullptr;
    Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
    const Vector3 at = camera ? camera->get_global_position() : Vector3();
    const Vector3 towards = get_direction_at(at);
    // Only once it moved a little: re-aiming the shadow map every frame makes
    // shadow edges crawl as the sun moves.
    if (light->get_global_basis().get_column(2).normalized().angle_to(towards) > LightStep)
    {
        const Vector3 helper = std::abs(towards.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
        const Vector3 x = helper.cross(towards).normalized();
        light->set_global_basis(Basis(x, towards.cross(x), towards));
    }
    const float size = std::min(get_angular_diameter_at(at), 90.0f);
    if (_physicalSize)
        light->set_param(Light3D::PARAM_SIZE, size);
    if (_reflects)
    {
        const float energy = _energy * get_phase_at(at);
        if (!Math::is_equal_approx(light->get_param(Light3D::PARAM_ENERGY), energy))
            light->set_param(Light3D::PARAM_ENERGY, energy);
    }
    // Through the air the camera is in: reddened towards the horizon, none
    // below it. The light's own color is kept as its color above the air (at
    // runtime only: in the editor it would be saved with the scene).
    if (Engine::get_singleton()->is_editor_hint())
        return;
    if (!_reflects)
        set_sky_parameter(viewport, "jar_sun_size", float(Math::deg_to_rad(size))); // the sky's sun disk
    // Shadows fade out as the star reaches the horizon (near a surface):
    // grazing light gives the worst shadow artifacts, and real low-sun shadows
    // are faint anyway.
    if (!light->has_meta(BaseShadowMeta))
        light->set_meta(BaseShadowMeta, light->get_param(Light3D::PARAM_SHADOW_OPACITY));
    float shadow = light->get_meta(BaseShadowMeta);
    if (JarVoxelUniverse *universe = JarVoxelUniverse::get_singleton())
        if (const JarCelestialBody *ground = universe->get_dominant_body(at);
            ground && ground->get_altitude(at) < ground->get_effective_rotating_frame_height())
        {
            const float elevation = towards.dot(ground->get_up_at(at));
            const float t = std::clamp(elevation / 0.12f, 0.0f, 1.0f); // fully on above ~7 degrees
            shadow *= t * t * (3.0f - 2.0f * t);
        }
    if (!Math::is_equal_approx(light->get_param(Light3D::PARAM_SHADOW_OPACITY), shadow))
        light->set_param(Light3D::PARAM_SHADOW_OPACITY, shadow);
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
    if (!_reflects)
        apply_space_exposure(camera, at, towards);
}

// In space and in sunlight the eye is adapted to the sunlit bodies: auto
// exposure may not brighten a dark frame as much as at night, or a planet
// against black space burns out to white. Raises the camera attributes'
// auto_exposure_min_sensitivity (the darkest scene luminance auto exposure
// adapts to) to space_min_sensitivity, blending in with altitude and out in a
// body's shadow. Near the ground (caves, night) the scene's own value is kept.
void JarBodyLight::apply_space_exposure(Camera3D *camera, const Vector3 &at, const Vector3 &towards)
{
    if (_spaceMinSensitivity <= 0.0f || camera == nullptr)
        return;
    Ref<CameraAttributesPractical> attributes = camera->get_attributes();
    if (attributes.is_null())
        if (Ref<World3D> world = camera->get_world_3d(); world.is_valid())
            attributes = world->get_camera_attributes();
    if (attributes.is_null() || !attributes->is_auto_exposure_enabled())
        return;
    if (!attributes->has_meta(BaseSensitivityMeta))
        attributes->set_meta(BaseSensitivityMeta, attributes->get_auto_exposure_min_sensitivity());
    const float base = attributes->get_meta(BaseSensitivityMeta);
    float space = 0.0f;
    if (JarVoxelUniverse *universe = JarVoxelUniverse::get_singleton())
    {
        const JarCelestialBody *near = universe->get_dominant_body(at);
        space = 1.0f;
        if (near != nullptr && near != get_body())
        {
            // Out of the air: from half the rotating frame's height to its top.
            const float height = std::max(near->get_effective_rotating_frame_height(), 1.0f);
            const float t = std::clamp((near->get_altitude(at) / height - 0.5f) * 2.0f, 0.0f, 1.0f);
            space = t * t * (3.0f - 2.0f * t);
            // In its shadow: a cylinder behind it, soft over a few percent of its radius.
            const Vector3 d = at - near->get_center();
            const float along = d.dot(towards);
            if (along < 0.0f)
            {
                const float radius = near->get_effective_radius();
                const float off = (d - towards * along).length();
                const float lit = std::clamp((off / std::max(radius, 1.0f) - 0.97f) / 0.06f, 0.0f, 1.0f);
                space *= lit * lit * (3.0f - 2.0f * lit);
            }
        }
    }
    const float sensitivity = base + (std::max(base, _spaceMinSensitivity) - base) * space;
    if (std::abs(attributes->get_auto_exposure_min_sensitivity() - sensitivity) > 0.5f)
        attributes->set_auto_exposure_min_sensitivity(sensitivity);
    // The stars get back what the lower exposure took from them: the sky
    // looks as it does at night (a shader sky reads jar_star_boost).
    set_sky_parameter(camera->get_viewport(), "jar_star_boost", base > 0.0f ? sensitivity / base : 1.0f);
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
    BIND_WATER(underwater_material, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial");
#undef BIND_WATER
    ClassDB::bind_method(D_METHOD("get_active_material"), &JarBodyWater::get_active_material);
    ClassDB::bind_method(D_METHOD("get_active_underwater_material"), &JarBodyWater::get_active_underwater_material);
}

Ref<ShaderMaterial> JarBodyWater::get_active_material()
{
    if (_material.is_valid())
        return _materialCopy.of(_material); // never the user's own (see JarMaterialCopy)
    if (_defaultMaterial.is_null())
        _defaultMaterial = make_material(WaterShader, 0);
    return _defaultMaterial;
}

Ref<ShaderMaterial> JarBodyWater::get_active_underwater_material()
{
    if (_underwaterMaterial.is_valid())
        return _underwaterCopy.of(_underwaterMaterial);
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
    if (frame.radial)
        body.extend_view_radius(level);
    const FarView view = frame.radial ? far_view(body) : FarView{};
    const bool scaled = frame.radial && view.is_scaled();
    for (const Ref<ShaderMaterial> &material : {get_active_material(), get_active_underwater_material()})
    {
        material->set_shader_parameter("water_center", scaled ? view.center : frame.center);
        material->set_shader_parameter("water_axis", frame.axis);
        material->set_shader_parameter("water_level", scaled ? level * view.scale : level);
        material->set_shader_parameter("water_radial", frame.radial);
        material->set_shader_parameter("view_scale", scaled ? view.scale : 1.0f);
        // Waves and foam move with the body when it turns in the scene.
        material->set_shader_parameter("world_to_body", body.get_global_basis().orthonormalized().transposed());
    }
    if (scaled)
        apply_far_light(get_active_material(), frame.center);
    const Vector3 at = eye(frame);
    _fog->set_global_position(at);
    if (scaled)
    {
        // Far away: only the sphere, scaled.
        if (!Math::is_equal_approx(_sphereRadius, level))
        {
            _far->set_mesh(make_sphere(level, 192, 96));
            _sphereRadius = level;
        }
        _far->set_global_transform(Transform3D(Basis().scaled(Vector3(view.scale, view.scale, view.scale)), view.center));
        _near->set_visible(false);
        set_hole(Vector3(), 0.0f);
    }
    else if (frame.radial)
    {
        if (!Math::is_equal_approx(_sphereRadius, level))
        {
            _far->set_mesh(make_sphere(level, 192, 96));
            _sphereRadius = level;
        }
        _far->set_global_transform(Transform3D(Basis(), frame.center));
        // The disc lies on the tangent plane under the camera and the shader
        // bends it onto the sphere: 4 radii out on the plane is 76 degrees
        // around it, past the horizon from up to 2 radii high. The coarse
        // sphere only draws what is left.
        constexpr float DiscRadii = 4.0f;
        if (!Math::is_equal_approx(_discRadius, level * DiscRadii))
        {
            _discRadius = level * DiscRadii;
            _near->set_mesh(make_disc(_discRadius));
        }
        const Vector3 up = (at - frame.center).normalized();
        _near->set_global_transform(Transform3D(basis_with_up(up), frame.center + up * level));
        const bool close = (at - frame.center).length() - level < 2.0f * level;
        _near->set_visible(close);
        // The chord of those 76 degrees.
        set_hole(_near->get_global_position(), close ? 2.0f * level * std::sin(0.5f * std::atan(DiscRadii)) * 0.99f : 0.0f);
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
        constexpr float FlatDisc = 200000.0f;
        if (!Math::is_equal_approx(_discRadius, FlatDisc))
        {
            _discRadius = FlatDisc;
            _near->set_mesh(make_disc(_discRadius));
        }
        _near->set_global_transform(Transform3D(basis, onPlane));
        _near->set_visible(true);
        set_hole(onPlane, FlatDisc * 0.99f);
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
        return _materialCopy.of(_material); // never the user's own (see JarMaterialCopy)
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

void JarBodyAtmosphere::apply_air(const Ref<ShaderMaterial> &material, const FarView &view) const
{
    const AtmosphereParams &p = _model.params();
    const double s = view.scale;
    const double thickness = (p.top - p.bottom) * s;
    // The model's per-meter coefficients at sea level.
    const AtmosphereModel::Medium sea = _model.medium(0.0);
    material->set_shader_parameter("planet_center", view.center);
    material->set_shader_parameter("planet_radius", float(p.bottom * s));
    material->set_shader_parameter("atmosphere_radius", float(p.top * s));
    material->set_shader_parameter("rayleigh_scattering", DTransform::to(sea.rayleighScattering / s));
    material->set_shader_parameter("rayleigh_scale_height", float(p.rayleighHeight * thickness));
    material->set_shader_parameter("mie_scattering", float(sea.mieScattering.x / s));
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
            set_sky_parameter(viewport, "jar_daylight", 0.0f);
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
    // How bright the sky is here: shader skies fade their stars with it.
    set_sky_parameter(viewport, "jar_daylight", std::max({sky.r, sky.g, sky.b}));
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
    body.extend_view_radius(outer);
    const FarView view = far_view(body);
    _shell->set_global_transform(Transform3D(Basis().scaled(Vector3(view.scale, view.scale, view.scale)), view.center));
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
        apply_air(m, view);
        apply_sun(m);
        // Seen from outside, towards the star from this planet (the light
        // is aimed at the camera, which may be on another body).
        if (!contains(eye(frame)))
            m->set_shader_parameter("sun_direction", star_direction_at(frame.center));
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
    ClassDB::bind_method(D_METHOD("get_thickness"), &JarBodyClouds::get_thickness);
    ClassDB::bind_method(D_METHOD("set_thickness", "value"), &JarBodyClouds::set_thickness);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "thickness", PROPERTY_HINT_RANGE, "1,10000,1,or_greater,suffix:m"),
                 "set_thickness", "get_thickness");
    ClassDB::bind_method(D_METHOD("get_seed"), &JarBodyClouds::get_seed);
    ClassDB::bind_method(D_METHOD("set_seed", "value"), &JarBodyClouds::set_seed);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
    ClassDB::bind_method(D_METHOD("get_weather"), &JarBodyClouds::get_weather);
    ClassDB::bind_method(D_METHOD("get_material"), &JarBodyClouds::get_material);
    ClassDB::bind_method(D_METHOD("set_material", "value"), &JarBodyClouds::set_material);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial"),
                 "set_material", "get_material");
    ClassDB::bind_method(D_METHOD("get_active_material"), &JarBodyClouds::get_active_material);
}

Ref<ShaderMaterial> JarBodyClouds::get_active_material()
{
    if (_material.is_valid())
        return _materialCopy.of(_material); // never the user's own (see JarMaterialCopy)
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
    _noise = CloudNoise::texture();
}

void JarBodyClouds::update(const JarCelestialBody &body)
{
    if (_shell == nullptr)
        return;
    const JarCelestialBody::Frame frame = body.get_frame();
    const float bottom = frame.seaLevel + _altitude;
    const float top = bottom + _thickness;
    const Ref<ShaderMaterial> material = get_active_material();
    FarView view;
    const Vector3 at = eye(frame);
    if (frame.radial)
    {
        body.extend_view_radius(top);
        view = far_view(body);
        // Drawn on the back faces of a sphere just outside the layer: from
        // inside they cover the screen, from outside the planet.
        if (!Math::is_equal_approx(_shellRadius, top * 1.02f))
        {
            _shellRadius = top * 1.02f;
            _shell->set_mesh(make_sphere(_shellRadius, 64, 32));
        }
        _shell->set_global_transform(Transform3D(Basis().scaled(Vector3(view.scale, view.scale, view.scale)), view.center));
        material->set_shader_parameter("planet_center", view.center);
    }
    else
    {
        // A box around the camera (the shader fades the slab out before its walls).
        constexpr float Box = 200000.0f;
        if (_shellRadius != 0.0f)
        {
            _shellRadius = 0.0f;
            Ref<BoxMesh> box;
            box.instantiate();
            box->set_size(Vector3(Box, Box, Box));
            _shell->set_mesh(box);
        }
        _shell->set_global_transform(Transform3D(Basis(), at));
        material->set_shader_parameter("planet_center", frame.center);
    }
    material->set_shader_parameter("cloud_bottom", bottom);
    material->set_shader_parameter("cloud_top", top);
    material->set_shader_parameter("ground_level", frame.seaLevel);
    material->set_shader_parameter("up_axis", frame.axis);
    material->set_shader_parameter("radial", frame.radial);
    material->set_shader_parameter("view_scale", view.scale);
    material->set_shader_parameter("cloud_noise", _noise);
    // Clouds and wind move with the body when it turns in the scene.
    const Basis toBody = body.get_global_basis().orthonormalized().transposed();
    material->set_shader_parameter("world_to_body", toBody);
    Vector3 axis = body.get_spin_axis();
    if (const auto *carrier = Object::cast_to<Node3D>(body.get_parent()); carrier && axis.length_squared() > 1e-12f)
        axis = carrier->get_global_basis().xform(axis);
    const Vector3 pole = axis.length_squared() > 1e-12f ? toBody.xform(axis).normalized() : Vector3(0, 1, 0);
    material->set_shader_parameter("spin_axis", pole);
    // The weather map: once (a fraction of a second), again only for another
    // seed or pole.
    // Its texels on a thread (half a second on all cores; the frame would
    // stall), the texture when they are done; no clouds until then.
    if (frame.radial && !_weatherJob.running() && (_weatherSeed != _seed || !_weatherPole.is_equal_approx(pole)))
    {
        _weatherSeed = _seed;
        _weatherPole = pole;
        const int seed = _seed;
        _weatherJob.start([seed, pole] { return CloudNoise::weather_faces(seed, pole); });
    }
    if (_weatherJob.ready())
        _weather = CloudNoise::weather_texture(_weatherJob.take());
    material->set_shader_parameter("weather_map", _weather);
    material->set_shader_parameter("weather_ready", !frame.radial || _weather.is_valid());
    // The sun: in scaled space the star's own light here; else the scene's
    // light (reddened by the air the camera is in), with its energy.
    Vector3 direction, color, floor;
    far_light(this, frame.center, direction, color, floor);
    const bool inside = !frame.radial || (at - frame.center).length() < top * 1.5f;
    if (!view.is_scaled() && inside)
        if (DirectionalLight3D *light = find_sun())
        {
            direction = light->get_global_basis().get_column(2).normalized();
            const Color c = light->get_color() * light->get_param(Light3D::PARAM_ENERGY);
            color = Vector3(c.r, c.g, c.b);
        }
    material->set_shader_parameter("sun_direction", direction);
    material->set_shader_parameter("sun_color", color);
    material->set_shader_parameter("ambient_light", floor);
    apply_sun(material); // the moon's light (moon_direction, moon_color)
    material->set_shader_parameter("sun_direction", direction);
    material->set_shader_parameter("sun_color", color);
}

