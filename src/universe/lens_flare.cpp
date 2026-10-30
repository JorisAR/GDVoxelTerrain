#include "lens_flare.h"
#include "body_layer.h"
#include <cmath>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/viewport.hpp>

namespace
{
const char *FlareShader = "res://addons/jar_voxel_terrain/nodes/lens_flare/lens_flare.gdshader";
}

void JarLensFlare::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_light"), &JarLensFlare::get_light);
    ClassDB::bind_method(D_METHOD("set_light", "value"), &JarLensFlare::set_light);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "light", PROPERTY_HINT_NODE_TYPE, "DirectionalLight3D"), "set_light",
                 "get_light");
    ClassDB::bind_method(D_METHOD("get_intensity"), &JarLensFlare::get_intensity);
    ClassDB::bind_method(D_METHOD("set_intensity", "value"), &JarLensFlare::set_intensity);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "intensity", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"),
                 "set_intensity", "get_intensity");
    ClassDB::bind_method(D_METHOD("get_material"), &JarLensFlare::get_material);
    ClassDB::bind_method(D_METHOD("set_material", "value"), &JarLensFlare::set_material);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial"),
                 "set_material", "get_material");
    ClassDB::bind_method(D_METHOD("get_active_material"), &JarLensFlare::get_active_material);
}

Ref<ShaderMaterial> JarLensFlare::get_active_material()
{
    if (_material.is_valid())
        return _material;
    if (_defaultMaterial.is_null())
    {
        _defaultMaterial.instantiate();
        _defaultMaterial->set_shader(ResourceLoader::get_singleton()->load(FlareShader));
        _defaultMaterial->set_render_priority(Material::RENDER_PRIORITY_MAX); // after the atmosphere
    }
    return _defaultMaterial;
}

void JarLensFlare::set_material(const Ref<ShaderMaterial> &value)
{
    _material = value;
    if (_quad)
        _quad->set_material_override(get_active_material());
}

void JarLensFlare::_notification(int what)
{
    if (Engine::get_singleton()->is_editor_hint())
        return;
    if (what == NOTIFICATION_READY)
    {
        Ref<QuadMesh> quad;
        quad.instantiate();
        quad->set_size(Vector2(2, 2)); // clip space: the whole screen
        _quad = memnew(MeshInstance3D);
        _quad->set_mesh(quad);
        _quad->set_as_top_level(true);
        _quad->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
        _quad->set_custom_aabb(AABB(Vector3(-1e7, -1e7, -1e7), Vector3(2e7, 2e7, 2e7))); // never culled
        _quad->set_material_override(get_active_material());
        add_child(_quad, false, INTERNAL_MODE_FRONT);
        set_process_internal(true);
    }
    else if (what == NOTIFICATION_INTERNAL_PROCESS)
        update();
}

DirectionalLight3D *JarLensFlare::find_light() const
{
    if (_light)
        return _light;
    SceneTree *tree = get_tree();
    if (tree == nullptr)
        return nullptr;
    const TypedArray<Node> stars = tree->get_nodes_in_group(JarBodyLight::StarGroup);
    if (!stars.is_empty())
        return Object::cast_to<DirectionalLight3D>(stars[0]);
    Node *scene = tree->get_current_scene();
    const TypedArray<Node> lights =
        scene ? scene->find_children("*", "DirectionalLight3D", true, false) : TypedArray<Node>();
    return lights.is_empty() ? nullptr : Object::cast_to<DirectionalLight3D>(lights[0]);
}

void JarLensFlare::update()
{
    Viewport *viewport = get_viewport();
    Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
    DirectionalLight3D *light = find_light();
    const bool shown = camera && light && light->is_visible_in_tree() && is_visible_in_tree();
    if (!shown)
    {
        _quad->set_visible(false);
        return;
    }
    const Vector3 eye = camera->get_global_position();
    const Vector3 towards = light->get_global_basis().get_column(2).normalized();
    const Vector3 target = eye + towards * (camera->get_near() * 10.0f + 1.0f);
    if (camera->is_position_behind(target))
    {
        _quad->set_visible(false);
        return;
    }
    _quad->set_visible(true);
    _quad->set_global_position(eye); // anywhere in view (it covers the screen)
    const Vector2 size = viewport->get_visible_rect().size;
    const Vector2 uv = camera->unproject_position(target) / size;
    // The sun's apparent radius in screen heights.
    const float fov = float(Math::deg_to_rad(camera->get_fov()));
    const float angular = float(Math::deg_to_rad(light->get_param(Light3D::PARAM_SIZE))) * 0.5f;
    const float radius = std::tan(angular) / std::tan(fov * 0.5f) * 0.5f;
    const Color c = light->get_color() * light->get_param(Light3D::PARAM_ENERGY) * _intensity;
    const Ref<ShaderMaterial> material = get_active_material();
    material->set_shader_parameter("sun_uv", uv);
    material->set_shader_parameter("sun_light", Vector3(c.r, c.g, c.b));
    material->set_shader_parameter("aspect", size.x / std::max(size.y, 1.0f));
    material->set_shader_parameter("sun_radius", radius);
}
