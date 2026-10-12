#ifndef JAR_POPULATION_RENDERING_H
#define JAR_POPULATION_RENDERING_H

#include <godot_cpp/classes/geometry_instance3d.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

using namespace godot;

// How features and details show a level of detail between two distances.
namespace PopulationRendering
{
// Material a multimesh of `mesh` draws with: the override, else its first surface's.
inline Ref<Material> effective_material(const Ref<Mesh> &mesh, const Ref<Material> &override)
{
    if (override.is_valid() || mesh.is_null() || mesh->get_surface_count() == 0)
        return override;
    return mesh->surface_get_material(0);
}

// Materials including addons/jar_voxel_terrain/shaders/jar_population.gdshaderinc
// hide instances outside their `jar_lod_range` themselves (per instance).
inline bool has_instance_lod(const Ref<Material> &material)
{
    const Ref<ShaderMaterial> shader_material = material;
    if (shader_material.is_null() || shader_material->get_shader().is_null())
        return false;
    return shader_material->get_shader()->get_code().contains("jar_population");
}

// Shows `instance` (a multimesh spread over `margin` meters around its center)
// from `begin` to `end` meters (end 0 = no limit): per node with Godot's
// visibility range, widened by the margin when the material switches per
// instance itself (from the instances' custom data, see write_custom).
inline void set_lod_range(GeometryInstance3D *instance, const Ref<Material> &material, float begin, float end,
                          float margin)
{
    if (has_instance_lod(material))
    {
        instance->set_visibility_range_begin(begin > 0.0f ? std::max(0.0f, begin - margin) : 0.0f);
        instance->set_visibility_range_end(end > 0.0f ? end + margin : 0.0f);
    }
    else
    {
        instance->set_visibility_range_begin(begin);
        instance->set_visibility_range_end(end);
    }
}

// Multimesh custom data per instance, as jar_population.gdshaderinc reads it:
// the ground color (linear; sqrt-encoded, 8 bits per channel, packed into an
// integer a float holds exactly), the LOD range, and 1.
inline void write_custom(float *out, const glm::vec3 &ground, float begin, float end)
{
    auto channel = [](float v) { return uint32_t(std::lround(std::sqrt(std::clamp(v, 0.0f, 1.0f)) * 255.0f)); };
    out[0] = float((channel(ground.x) << 16) | (channel(ground.y) << 8) | channel(ground.z));
    out[1] = begin;
    out[2] = end > 0.0f ? end : 1e20f;
    out[3] = 1.0f;
}
} // namespace PopulationRendering

#endif // JAR_POPULATION_RENDERING_H
