#ifndef JAR_GROUND_PALETTE_H
#define JAR_GROUND_PALETTE_H

#include "biome_map.h"
#include <algorithm>
#include "terrain_material.h"
#include "up_field.h"
#include <glm/glm.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <memory>
#include <vector>

using namespace godot;

// Colors of the ground features and details stand on, for their materials to
// blend into (jar_ground_color() in jar_population.gdshaderinc): the ground
// rule's material of each biome (or of the terrain shader), as linear albedo
// (texture averages included). Built on the main thread whenever the volume's
// materials change; read-only afterwards, so worker threads share it.
class GroundPalette
{
  private:
    glm::vec3 _fallback{0.3f, 0.35f, 0.2f};
    std::vector<glm::vec3> _biomes; // per biome of the map
    // The terrain shader's peak rule: above this the ground is its peak
    // material (snow). Infinite without one.
    float _peakHeight = 1e30f;
    float _peakBlend = 1.0f;
    Ref<JarBiomeMap> _map;

  public:
    static std::shared_ptr<const GroundPalette> build(const TypedArray<JarTerrainMaterial> &materials,
                                                      const Ref<JarBiomeMap> &biomes,
                                                      const Ref<Material> &terrainMaterial);
    // Linear color of the ground at a volume-space position.
    glm::vec3 color_at(const glm::vec3 &position, const UpField &up) const;
    // 1 on ordinary ground, 0 where the shader shows peaks, at a height above
    // sea level: population rules agree with the ground they stand on.
    float below_peaks(float height) const
    {
        const float t = std::clamp((height - _peakHeight + _peakBlend) / (2.0f * _peakBlend), 0.0f, 1.0f);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }
    // The same, from biome weights already evaluated there.
    glm::vec3 color_for(const BiomeWeights &weights) const;
};

#endif // JAR_GROUND_PALETTE_H
