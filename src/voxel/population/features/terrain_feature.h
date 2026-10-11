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
// Rendered either as a `mesh` (instanced, cheapest) or as a `scene`
// (instantiated per feature: for colliders, scripts, LOD setups).
class JarTerrainFeature : public JarTerrainPopulator
{
    GDCLASS(JarTerrainFeature, JarTerrainPopulator);

  private:
    Ref<Mesh> mesh;
    Ref<Material> material;
    Ref<PackedScene> scene;
    float density = 0.01f;         // instances per square meter where the rules allow
    float draw_distance = 500.0f;  // meters
    bool cast_shadows = true;

  public:
    Ref<Mesh> get_mesh() const { return mesh; }
    void set_mesh(const Ref<Mesh> &value) { mesh = value; }
    Ref<Material> get_material() const { return material; }
    void set_material(const Ref<Material> &value) { material = value; }
    Ref<PackedScene> get_scene() const { return scene; }
    void set_scene(const Ref<PackedScene> &value) { scene = value; }
    float get_density() const { return density; }
    void set_density(float value) { density = std::max(0.0f, value); }
    float get_draw_distance() const { return draw_distance; }
    void set_draw_distance(float value) { draw_distance = std::max(1.0f, value); }
    bool get_cast_shadows() const { return cast_shadows; }
    void set_cast_shadows(bool value) { cast_shadows = value; }

  protected:
    static void _bind_methods()
    {
#define BIND_FEATURE(m_name, m_type, m_hint, m_hint_text)                                                              \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarTerrainFeature::get_##m_name);                                  \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarTerrainFeature::set_##m_name);                         \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
        BIND_FEATURE(mesh, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Mesh");
        BIND_FEATURE(material, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Material");
        BIND_FEATURE(scene, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "PackedScene");
        BIND_FEATURE(density, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.0001,or_greater,suffix:/m²");
        BIND_FEATURE(draw_distance, Variant::FLOAT, PROPERTY_HINT_RANGE, "1,5000,1,or_greater,suffix:m");
        BIND_FEATURE(cast_shadows, Variant::BOOL, PROPERTY_HINT_NONE, "");
#undef BIND_FEATURE
    }
};

#endif // JAR_TERRAIN_FEATURE_H
