#include "lens_flare.h"
#include "body_layer.h"
#include <cmath>
#include <algorithm>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/quad_mesh.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/viewport.hpp>

namespace
{
const char *FlareShader = "res://addons/jar_voxel_terrain/nodes/lens_flare/lens_flare.gdshader";
constexpr int MaxGhosts = 8; // MAX_GHOSTS in the shader

// A repeatable number in [0, 1) for a ghost's property.
float ghost_random(int seed, int ghost, int property)
{
    uint32_t h = uint32_t(seed) * 0x9E3779B9U ^ uint32_t(ghost + 1) * 0x8da6b343U ^ uint32_t(property + 1) * 0xd8163841U;
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return float(h) / 4294967296.0f;
}
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
    ClassDB::bind_method(D_METHOD("get_editor_preview"), &JarLensFlare::get_editor_preview);
    ClassDB::bind_method(D_METHOD("set_editor_preview", "value"), &JarLensFlare::set_editor_preview);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "editor_preview"), "set_editor_preview", "get_editor_preview");

#define BIND_ONE(m_name, m_value, m_range)                                                                             \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarLensFlare::get_##m_name);                                       \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarLensFlare::set_##m_name);
    JAR_FLARE_FLOATS(BIND_ONE)
#undef BIND_ONE
#define FLOAT_PROPERTY(m_name, m_range)                                                                                \
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, #m_name, PROPERTY_HINT_RANGE, m_range), "set_" #m_name, "get_" #m_name)
#define PROPERTY_OF(m_name, m_value, m_range) FLOAT_PROPERTY(m_name, m_range);
    ADD_GROUP("Glow", "glow_");
    PROPERTY_OF(glow_strength, 0, "0,2,0.01,or_greater")
    PROPERTY_OF(glow_size, 0, "0.001,1,0.001,or_greater")
    ADD_GROUP("Starburst", "starburst_");
    PROPERTY_OF(starburst_strength, 0, "0,2,0.01,or_greater")
    PROPERTY_OF(starburst_size, 0, "0.001,1,0.001,or_greater")
    PROPERTY_OF(starburst_sharpness, 0, "1,128,0.5")
    PROPERTY_OF(starburst_rotation, 0, "-180,180,0.1,radians_as_degrees")
    ClassDB::bind_method(D_METHOD("get_starburst_rays"), &JarLensFlare::get_starburst_rays);
    ClassDB::bind_method(D_METHOD("set_starburst_rays", "value"), &JarLensFlare::set_starburst_rays);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "starburst_rays", PROPERTY_HINT_RANGE, "0,16,1"), "set_starburst_rays",
                 "get_starburst_rays");
    ADD_GROUP("Streak", "streak_");
    PROPERTY_OF(streak_strength, 0, "0,2,0.01,or_greater")
    PROPERTY_OF(streak_width, 0, "0.0005,0.05,0.0001")
    PROPERTY_OF(streak_length, 0, "0.01,2,0.01,or_greater")
    ClassDB::bind_method(D_METHOD("get_streak_tint"), &JarLensFlare::get_streak_tint);
    ClassDB::bind_method(D_METHOD("set_streak_tint", "value"), &JarLensFlare::set_streak_tint);
    ADD_PROPERTY(PropertyInfo(Variant::COLOR, "streak_tint", PROPERTY_HINT_COLOR_NO_ALPHA), "set_streak_tint",
                 "get_streak_tint");
    ADD_GROUP("Halo", "halo_");
    PROPERTY_OF(halo_strength, 0, "0,1,0.001,or_greater")
    PROPERTY_OF(halo_radius, 0, "0.05,1,0.01,or_greater")
    PROPERTY_OF(halo_width, 0, "0.002,0.2,0.001")
    PROPERTY_OF(halo_chromatic, 0, "0,0.2,0.001")
    PROPERTY_OF(halo_arc, 0, "0,1,0.01")
    ADD_GROUP("Ghosts", "ghost_");
    PROPERTY_OF(ghost_strength, 0, "0,1,0.001,or_greater")
    ClassDB::bind_method(D_METHOD("get_ghost_count"), &JarLensFlare::get_ghost_count);
    ClassDB::bind_method(D_METHOD("set_ghost_count", "value"), &JarLensFlare::set_ghost_count);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "ghost_count", PROPERTY_HINT_RANGE, "0,8,1"), "set_ghost_count", "get_ghost_count");
    ClassDB::bind_method(D_METHOD("get_ghost_seed"), &JarLensFlare::get_ghost_seed);
    ClassDB::bind_method(D_METHOD("set_ghost_seed", "value"), &JarLensFlare::set_ghost_seed);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "ghost_seed", PROPERTY_HINT_RANGE, "0,1000,1,or_greater"), "set_ghost_seed",
                 "get_ghost_seed");
    ClassDB::bind_method(D_METHOD("get_ghost_size"), &JarLensFlare::get_ghost_size);
    ClassDB::bind_method(D_METHOD("set_ghost_size", "value"), &JarLensFlare::set_ghost_size);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ghost_size", PROPERTY_HINT_RANGE, "0.005,0.3,0.001,or_greater"), "set_ghost_size",
                 "get_ghost_size");
    ClassDB::bind_method(D_METHOD("get_ghost_spread"), &JarLensFlare::get_ghost_spread);
    ClassDB::bind_method(D_METHOD("set_ghost_spread", "value"), &JarLensFlare::set_ghost_spread);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ghost_spread", PROPERTY_HINT_RANGE, "0.1,2,0.01,or_greater"), "set_ghost_spread",
                 "get_ghost_spread");
    ClassDB::bind_method(D_METHOD("get_ghost_saturation"), &JarLensFlare::get_ghost_saturation);
    ClassDB::bind_method(D_METHOD("set_ghost_saturation", "value"), &JarLensFlare::set_ghost_saturation);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ghost_saturation", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_ghost_saturation",
                 "get_ghost_saturation");
    PROPERTY_OF(ghost_softness, 0, "0,1,0.01")
    PROPERTY_OF(ghost_fill, 0, "0,1,0.01")
    PROPERTY_OF(ghost_chromatic, 0, "0,0.2,0.001")
    ClassDB::bind_method(D_METHOD("get_ghost_sides"), &JarLensFlare::get_ghost_sides);
    ClassDB::bind_method(D_METHOD("set_ghost_sides", "value"), &JarLensFlare::set_ghost_sides);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "ghost_sides", PROPERTY_HINT_RANGE, "0,12,1"), "set_ghost_sides", "get_ghost_sides");
    PROPERTY_OF(ghost_rotation, 0, "-180,180,0.1,radians_as_degrees")
    ADD_GROUP("Occlusion", "occlusion_");
    PROPERTY_OF(occlusion_size, 0, "0.5,8,0.01")
#undef PROPERTY_OF
#undef FLOAT_PROPERTY
}

void JarLensFlare::set_starburst_rays(int value)
{
    _starburstRays = std::max(0, value);
    _lookDirty = true;
}

void JarLensFlare::set_streak_tint(const Color &value)
{
    _streakTint = value;
    _lookDirty = true;
}

void JarLensFlare::set_ghost_count(int value)
{
    _ghostCount = std::clamp(value, 0, MaxGhosts);
    _lookDirty = true;
}

void JarLensFlare::set_ghost_seed(int value)
{
    _ghostSeed = value;
    _lookDirty = true;
}

void JarLensFlare::set_ghost_sides(int value)
{
    _ghostSides = std::max(0, value);
    _lookDirty = true;
}

void JarLensFlare::set_ghost_size(float value)
{
    _ghostSize = std::max(0.001f, value);
    _lookDirty = true;
}

void JarLensFlare::set_ghost_spread(float value)
{
    _ghostSpread = std::max(0.0f, value);
    _lookDirty = true;
}

void JarLensFlare::set_ghost_saturation(float value)
{
    _ghostSaturation = std::clamp(value, 0.0f, 1.0f);
    _lookDirty = true;
}

void JarLensFlare::set_editor_preview(bool value)
{
    _editorPreview = value;
    if (_quad != nullptr && !value && Engine::get_singleton()->is_editor_hint())
        _quad->set_visible(false);
}

void JarLensFlare::apply_look()
{
    const Ref<ShaderMaterial> material = get_active_material();
#define SET_ONE(name, value, range) material->set_shader_parameter(#name, _##name);
    JAR_FLARE_FLOATS(SET_ONE)
#undef SET_ONE
    material->set_shader_parameter("starburst_rays", _starburstRays);
    material->set_shader_parameter("streak_tint", Vector3(_streakTint.r, _streakTint.g, _streakTint.b));
    // Ghosts are scattered along the line through the screen's center: 0 is
    // the sun, 1 the center, 2 the sun's mirror image; none on the far side of
    // the sun, and small ones are fainter. A seed gives another set.
    PackedFloat32Array positions, sizes;
    PackedVector3Array tints;
    for (int i = 0; i < MaxGhosts; ++i)
    {
        const float place = (float(i) + ghost_random(_ghostSeed, i, 0)) / float(MaxGhosts);
        positions.push_back(std::max(0.25f, 1.0f + (place * 2.0f - 1.0f) * 0.85f * _ghostSpread));
        const float scale = ghost_random(_ghostSeed, i, 1);
        sizes.push_back(_ghostSize * (0.4f + 1.4f * scale));
        Color tint;
        tint.set_hsv(ghost_random(_ghostSeed, i, 2), _ghostSaturation, 0.5f + 0.5f * scale);
        tints.push_back(Vector3(tint.r, tint.g, tint.b));
    }
    material->set_shader_parameter("ghost_count", _ghostCount);
    material->set_shader_parameter("ghost_positions", positions);
    material->set_shader_parameter("ghost_sizes", sizes);
    material->set_shader_parameter("ghost_tints", tints);
    _lookDirty = false;
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
    _lookDirty = true;
}

void JarLensFlare::_notification(int what)
{
    if (what == NOTIFICATION_READY)
    {
        make_quad();
        set_process_internal(true);
    }
    else if (what == NOTIFICATION_INTERNAL_PROCESS)
        update();
}

void JarLensFlare::make_quad()
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
}

Camera3D *JarLensFlare::current_camera() const
{
    if (Engine::get_singleton()->is_editor_hint())
    {
        if (EditorInterface *editor = EditorInterface::get_singleton())
            if (SubViewport *viewport = editor->get_editor_viewport_3d(0))
                return viewport->get_camera_3d();
        return nullptr;
    }
    Viewport *viewport = get_viewport();
    return viewport ? viewport->get_camera_3d() : nullptr;
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
    if (Engine::get_singleton()->is_editor_hint())
        if (EditorInterface *editor = EditorInterface::get_singleton())
            scene = editor->get_edited_scene_root();
    const TypedArray<Node> lights =
        scene ? scene->find_children("*", "DirectionalLight3D", true, false) : TypedArray<Node>();
    return lights.is_empty() ? nullptr : Object::cast_to<DirectionalLight3D>(lights[0]);
}

void JarLensFlare::update()
{
    Camera3D *camera = current_camera();
    DirectionalLight3D *light = find_light();
    const bool previewing = !Engine::get_singleton()->is_editor_hint() || _editorPreview;
    const bool shown = previewing && camera && light && light->is_visible_in_tree() && is_visible_in_tree();
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
    if (_lookDirty)
        apply_look();
    const Vector2 size = camera->get_viewport()->get_visible_rect().size;
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
