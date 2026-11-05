#ifndef JAR_TERRAIN_FEATURE_H
#define JAR_TERRAIN_FEATURE_H

#include "terrain_populator.h"
#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/packed_scene.hpp>

using namespace godot;

// Large objects (trees, rocks, structures) placed on the volume's SDF surface.
// Unlike details they do not depend on the mesh resolution, so they are seen
// from afar and always stand in the same place; edits remove the ones they
// touch, for good.
//
// Rendered as a `mesh` (instanced), or from a `scene`. A scene is a template
// by default (SCENE_MODE_BATCHED): each of its MeshInstance3Ds is drawn for all
// features at once (multimeshes), between its own visibility range (LODs,
// impostors), and its collision shapes become colliders near viewers. With
// SCENE_MODE_NODES the scene is instantiated per feature instead (scripts,
// lights, anything else), which costs far more.
//
// A baked `impostor` (see JarImpostorBaker; the Bake button of the inspector)
// replaces everything from `impostor_distance` on, and anything a scene shows
// beyond that distance.
//
// Materials that include addons/jar_voxel_terrain/shaders/jar_population.gdshaderinc
// switch LODs per instance and can blend with the ground's color.
class JarTerrainFeature : public JarTerrainPopulator
{
    GDCLASS(JarTerrainFeature, JarTerrainPopulator);

  public:
    enum SceneMode
    {
        SCENE_MODE_BATCHED,
        SCENE_MODE_NODES,
    };

  private:
    Ref<Mesh> mesh;
    Ref<Material> material;
    Ref<PackedScene> scene;
    int scene_mode = SCENE_MODE_BATCHED;
    float collision_distance = 64.0f; // meters; batched scenes' collision shapes
    float density = 0.01f;         // instances per square meter where the rules allow
    float footprint = 2.0f;        // meters kept free of other features
    float draw_distance = 500.0f;  // meters
    bool cast_shadows = true;
    Ref<Mesh> impostor;
    float impostor_distance = 0.0f; // meters; 0: where its pixels get as large as the screen's

  public:
    Ref<Mesh> get_mesh() const { return mesh; }
    void set_mesh(const Ref<Mesh> &value) { mesh = value; }
    Ref<Material> get_material() const { return material; }
    void set_material(const Ref<Material> &value) { material = value; }
    Ref<PackedScene> get_scene() const { return scene; }
    void set_scene(const Ref<PackedScene> &value)
    {
        scene = value;
        notify_property_list_changed();
    }
    int get_scene_mode() const { return scene_mode; }
    void set_scene_mode(int value)
    {
        scene_mode = value;
        notify_property_list_changed();
    }
    float get_collision_distance() const { return collision_distance; }
    void set_collision_distance(float value) { collision_distance = std::max(0.0f, value); }
    float get_density() const { return density; }
    float get_footprint() const { return footprint; }
    void set_footprint(float value) { footprint = std::max(0.0f, value); }
    void set_density(float value) { density = std::max(0.0f, value); }
    float get_draw_distance() const { return draw_distance; }
    void set_draw_distance(float value) { draw_distance = std::max(1.0f, value); }
    bool get_cast_shadows() const { return cast_shadows; }
    void set_cast_shadows(bool value) { cast_shadows = value; }
    Ref<Mesh> get_impostor() const { return impostor; }
    void set_impostor(const Ref<Mesh> &value) { impostor = value; }
    float get_impostor_distance() const { return impostor_distance; }
    void set_impostor_distance(float value) { impostor_distance = std::max(0.0f, value); }
    // Where the impostor starts: 3 times its size unless set (a 256 pixel
    // frame is about as sharp as the screen there).
    float effective_impostor_distance() const
    {
        if (impostor_distance > 0.0f || impostor.is_null())
            return impostor_distance;
        return 3.0f * float(impostor->get_aabb().get_longest_axis_size());
    }

  protected:
    // A scene replaces the mesh settings.
    void _validate_property(PropertyInfo &property) const
    {
        JarTerrainPopulator::_validate_property(property);
        const StringName name = property.name;
        if (scene.is_valid() && (name == StringName("mesh") || name == StringName("material") ||
                                 name == StringName("cast_shadows")))
            property.usage = PROPERTY_USAGE_NO_EDITOR;
        if ((scene.is_null() && name == StringName("scene_mode")) ||
            ((scene.is_null() || scene_mode != SCENE_MODE_BATCHED) && name == StringName("collision_distance")))
            property.usage = PROPERTY_USAGE_NO_EDITOR;
    }

    static void _bind_methods()
    {
#define BIND_FEATURE(m_name, m_type, m_hint, m_hint_text)                                                              \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarTerrainFeature::get_##m_name);                                  \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarTerrainFeature::set_##m_name);                         \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
        BIND_FEATURE(mesh, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Mesh");
        BIND_FEATURE(material, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Material");
        BIND_FEATURE(scene, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "PackedScene");
        BIND_FEATURE(scene_mode, Variant::INT, PROPERTY_HINT_ENUM, "Batched,Nodes");
        BIND_FEATURE(collision_distance, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,500,1,or_greater,suffix:m");
        BIND_FEATURE(density, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.0001,or_greater,suffix:/m2");
        BIND_FEATURE(footprint, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,50,0.1,or_greater,suffix:m");
        BIND_FEATURE(draw_distance, Variant::FLOAT, PROPERTY_HINT_RANGE, "1,5000,1,or_greater,suffix:m");
        BIND_FEATURE(cast_shadows, Variant::BOOL, PROPERTY_HINT_NONE, "");
        BIND_FEATURE(impostor, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Mesh");
        BIND_FEATURE(impostor_distance, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,2000,1,or_greater,suffix:m");
#undef BIND_FEATURE
        BIND_ENUM_CONSTANT(SCENE_MODE_BATCHED);
        BIND_ENUM_CONSTANT(SCENE_MODE_NODES);
    }
};

VARIANT_ENUM_CAST(JarTerrainFeature::SceneMode);

#endif // JAR_TERRAIN_FEATURE_H
