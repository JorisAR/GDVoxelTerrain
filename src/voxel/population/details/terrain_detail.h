#ifndef JAR_TERRAIN_DETAIL_H
#define JAR_TERRAIN_DETAIL_H

#include "terrain_populator.h"
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>

#include <algorithm>
#include <cmath>

using namespace godot;

// How far a detail goes, in meters. Past `far` it thins out, gradually, down to
// `density` of it at `draw`: each instance has a place in [0, 1) that says how
// far it lasts, so they go one by one, not in one ring.
struct DetailLevels
{
    float draw = 0.0f;
    float far = 0.0f; // 0: the same all the way
    float density = 1.0f;

    // The part of them still there at `distance`.
    float alive_at(float distance) const
    {
        if (far <= 0.0f || distance <= far)
            return 1.0f;
        const float t = 1.0f - std::min((distance - far) / std::max(draw - far, 1e-3f), 1.0f);
        return density + (1.0f - density) * t * t;
    }
    // How far the instance at `place` lasts.
    float last_distance(float place) const
    {
        if (far <= 0.0f || place < density)
            return draw;
        return far + (1.0f - std::sqrt((place - density) / std::max(1.0f - density, 1e-3f))) * (draw - far);
    }
};

// Small, dense objects (grass, pebbles) scattered over full-detail chunks. They
// are generated from the chunk mesh, so they follow edits automatically, and
// are not persistent: their exact positions may change when regenerated.
// How far they go follows from their size (see JarVoxelPopulation), and
// `draw_distance`, `far_distance` and `far_density` override it, 0 meaning
// automatic. Beyond the far distance they are drawn as `far_mesh` (fewer
// triangles) if it has one, a little larger, and thin out gradually down to the
// far density at the draw distance: the same instances, so nothing moves, and
// chunks only make what is left at their distance. Materials that include
// addons/jar_voxel_terrain/shaders/jar_population.gdshaderinc switch per
// instance and can take the ground's color; others switch per chunk.
class JarTerrainDetail : public JarTerrainPopulator
{
    GDCLASS(JarTerrainDetail, JarTerrainPopulator);

  private:
    Ref<Mesh> mesh;
    Ref<Material> material;
    Ref<Mesh> far_mesh;
    float far_distance = 0.0f;     // meters; 0: from its size
    float far_density = 0.0f;      // part of them drawn beyond it; 0: the population's
    float density = 1.0f;          // instances per square meter
    float draw_distance = 0.0f;    // meters; 0: from its size
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
    float get_far_density() const { return far_density; }
    void set_far_density(float value) { far_density = std::clamp(value, 0.0f, 1.0f); }
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
        BIND_DETAIL(far_density, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
        BIND_DETAIL(density, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,20,0.01,or_greater,suffix:/m2");
        BIND_DETAIL(draw_distance, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,500,0.1,or_greater,suffix:m");
        BIND_DETAIL(shadows_enabled, Variant::BOOL, PROPERTY_HINT_NONE, "");
#undef BIND_DETAIL
    }
};

#endif // JAR_TERRAIN_DETAIL_H
