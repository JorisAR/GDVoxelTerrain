#ifndef JAR_TERRAIN_MATERIAL_PALETTE_H
#define JAR_TERRAIN_MATERIAL_PALETTE_H

#include "terrain_material.h"
#include "biome_map.h"
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;

// Packs a volume's JarTerrainMaterials into the shader inputs every terrain
// shader reads (see addons/jar_voxel_terrain/src/shaders/terrain_common.gdshaderinc):
//   jar_palette          256x2 RGBA float texture; for material i:
//                          (i, 0) = linear albedo rgb, roughness
//                          (i, 1) = 1 / texture size, albedo layer, normal layer, variation
//                        (layers are -1 without a texture)
//   jar_albedo_textures  Texture2DArray of all albedo textures (if any)
//   jar_normal_textures  Texture2DArray of all normal maps (if any)
//   jar_material_count   number of materials
//   jar_biome_count      number of biomes (0 without a biome map); for biome b < 4:
//                          (b, 2) = ground, cliff, shore, seabed material, (b, 3).x = peak
//                        (-1 keeps the shader's own rule material)
// Textures are converted to one size and format (the first texture's size).
namespace TerrainMaterialPalette
{
void apply(const TypedArray<JarTerrainMaterial> &materials, const Ref<JarBiomeMap> &biomes,
           const Ref<ShaderMaterial> &target);
// Used when a volume has no materials: 0 grass, 1 rock, 2 sand, 3 seabed,
// 4 snow (the default ground rules), then 5 dirt, 6 dark stone, 7 clay.
TypedArray<JarTerrainMaterial> default_materials();
}

#endif // JAR_TERRAIN_MATERIAL_PALETTE_H
