#ifndef JAR_GROUND_PALETTE_H
#define JAR_GROUND_PALETTE_H

#include "biome_map.h"
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
    Ref<JarBiomeMap> _map;

  public:
    static std::shared_ptr<const GroundPalette> build(const TypedArray<JarTerrainMaterial> &materials,
                                                      const Ref<JarBiomeMap> &biomes,
                                                      const Ref<Material> &terrainMaterial);
    // Linear color of the ground at a volume-space position.
    glm::vec3 color_at(const glm::vec3 &position, const UpField &up) const;
};

#endif // JAR_GROUND_PALETTE_H
