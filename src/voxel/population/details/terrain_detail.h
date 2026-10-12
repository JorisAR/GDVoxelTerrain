#ifndef JAR_TERRAIN_DETAIL_H
#define JAR_TERRAIN_DETAIL_H

#include "terrain_populator.h"
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>

using namespace godot;

// Small, dense objects (grass, pebbles) scattered over full-detail chunks. They
// are generated from the chunk mesh, so they follow edits automatically, and
// are not persistent: their exact positions may change when regenerated.
// An optional `far_mesh` (fewer triangles) replaces `mesh` beyond
// `far_distance`. Materials that include
// addons/jar_voxel_terrain/shaders/jar_population.gdshaderinc switch per
// instance and can take the ground's color; others switch per chunk.
class JarTerrainDetail : public JarTerrainPopulator
{
    GDCLASS(JarTerrainDetail, JarTerrainPopulator);

  private:
    Ref<Mesh> mesh;
    Ref<Material> material;
    Ref<Mesh> far_mesh;
    float far_distance = 0.0f;     // meters; 0 = no far mesh
    float density = 1.0f;          // instances per square meter
    float draw_distance = 0.0f;    // meters; 0 shows them on every full-detail chunk
    bool shadows_enabled = false;

  public:
    Ref<Mesh> get_mesh() const { return mesh; }
    void set_mesh(const Ref<Mesh> &value) { mesh = value; }
    Ref<Material> get_material() const { return material; }
    void set_material(const Ref<Material> &value) { material = value; }
    Ref<Mesh> get_far_mesh() const { return far_mesh; }
    void set_far_mesh(const Ref<Mesh> &value) { far_mesh = value; }
    float get_far_distance() const { return far_distance; }
    void set_far_distance(float value) { far_distance = std::max(0.0f, value); }
    float get_density() const { return density; }
    void set_density(float value) { density = std::max(0.0f, value); }
    float get_draw_distance() const { return draw_distance; }
    void set_draw_distance(float value) { draw_distance = std::max(0.0f, value); }
    bool get_shadows_enabled() const { return shadows_enabled; }
    void set_shadows_enabled(bool value) { shadows_enabled = value; }

  protected:
    void _validate_property(PropertyInfo &property) const
    {
        JarTerrainPopulator::_validate_property(property);
    }

    static void _bind_methods()
    {
#define BIND_DETAIL(m_name, m_type, m_hint, m_hint_text)                                                               \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarTerrainDetail::get_##m_name);                                   \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarTerrainDetail::set_##m_name);                          \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
        BIND_DETAIL(mesh, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Mesh");
        BIND_DETAIL(material, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Material");
        BIND_DETAIL(far_mesh, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Mesh");
        BIND_DETAIL(far_distance, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,500,0.1,or_greater,suffix:m");
        BIND_DETAIL(density, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,20,0.01,or_greater,suffix:/m²");
        BIND_DETAIL(draw_distance, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,500,0.1,or_greater,suffix:m");
        BIND_DETAIL(shadows_enabled, Variant::BOOL, PROPERTY_HINT_NONE, "");
#undef BIND_DETAIL
    }
};

#endif // JAR_TERRAIN_DETAIL_H
