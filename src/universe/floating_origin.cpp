#include "floating_origin.h"
#include "voxel_chunk.h"
#include "voxel_universe.h"
#include "voxel_viewer.h"
#include <cmath>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>

const char *FloatingOrigin::DistanceSetting = "jar_voxel_terrain/floating_origin/distance";

void FloatingOrigin::register_settings()
{
    ProjectSettings *settings = ProjectSettings::get_singleton();
    const double value = 2048.0;
    if (!settings->has_setting(DistanceSetting))
        settings->set_setting(DistanceSetting, value);
    settings->set_initial_value(DistanceSetting, value);
    settings->set_as_basic(DistanceSetting, true);
    Dictionary info;
    info["name"] = DistanceSetting;
    info["type"] = Variant::FLOAT;
    info["hint"] = PROPERTY_HINT_RANGE;
    info["hint_string"] = "0,100000,1,or_greater,suffix:m";
    settings->add_property_info(info);
}

double FloatingOrigin::distance()
{
    ProjectSettings *settings = ProjectSettings::get_singleton();
    return settings && settings->has_setting(DistanceSetting)
               ? double(settings->get_setting_with_override(DistanceSetting))
               : 2048.0;
}

namespace
{
void shift_node(Node3D *node, const Transform3D &shift, bool turns)
{
    node->set_global_transform(shift * node->get_global_transform());
    if (!turns)
        return;
    if (auto *rigid = Object::cast_to<RigidBody3D>(node))
    {
        rigid->set_linear_velocity(shift.basis.xform(rigid->get_linear_velocity()));
        rigid->set_angular_velocity(shift.basis.xform(rigid->get_angular_velocity()));
    }
    else if (auto *character = Object::cast_to<CharacterBody3D>(node))
    {
        character->set_velocity(shift.basis.xform(character->get_velocity()));
        character->set_up_direction(shift.basis.xform(character->get_up_direction()).normalized());
    }
}

void visit(Node *node, const Transform3D &shift, bool turns, const std::unordered_set<uint64_t> &skip)
{
    const int count = node->get_child_count();
    for (int i = 0; i < count; ++i)
    {
        Node *child = node->get_child(i);
        // Chunks hold no top-level nodes; there are thousands of them.
        if (Object::cast_to<JarVoxelChunk>(child) != nullptr)
            continue;
        if (auto *spatial = Object::cast_to<Node3D>(child))
        {
            // Its pose follows its parent's, unless the parent is no Node3D
            // or it is top level: those are moved themselves.
            const bool follows = !spatial->is_set_as_top_level() && Object::cast_to<Node3D>(node) != nullptr;
            if (!follows && !skip.count(spatial->get_instance_id()))
                shift_node(spatial, shift, turns);
        }
        visit(child, shift, turns, skip);
    }
}

// The first enabled viewer, else the camera.
bool viewer_position(Node *context, Vector3 &at)
{
    for (const JarVoxelViewer *viewer : JarVoxelViewer::get_viewers())
        if (viewer->is_enabled() && viewer->is_inside_tree())
        {
            at = viewer->get_global_position();
            return true;
        }
    if (Viewport *viewport = context->is_inside_tree() ? context->get_viewport() : nullptr)
        if (Camera3D *camera = viewport->get_camera_3d())
        {
            at = camera->get_global_position();
            return true;
        }
    return false;
}
} // namespace

void FloatingOrigin::apply(Node *root, const Transform3D &shift, const std::unordered_set<uint64_t> &skip)
{
    if (root == nullptr)
        return;
    visit(root, shift, !shift.basis.is_equal_approx(Basis()), skip);
    if (JarVoxelUniverse *universe = JarVoxelUniverse::get_singleton())
        universe->emit_signal("origin_shifted", shift);
}

bool FloatingOrigin::update_without_universe(Node *context)
{
    static uint64_t lastFrame = UINT64_MAX;
    const double limit = distance();
    if (limit <= 0.0 || context == nullptr || !context->is_inside_tree())
        return false;
    const uint64_t frame = Engine::get_singleton()->get_physics_frames();
    if (frame == lastFrame)
        return false;
    lastFrame = frame;
    Vector3 at;
    if (!viewer_position(context, at) || at.length() < limit)
        return false;
    // Whole meters: positions that were whole meters apart stay exactly so.
    const Vector3 move(-std::round(at.x), -std::round(at.y), -std::round(at.z));
    apply(context->get_tree()->get_root(), Transform3D(Basis(), move), {});
    return true;
}
