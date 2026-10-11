#ifndef MATERIAL_MESH_BUILDER_H
#define MATERIAL_MESH_BUILDER_H

#include "material_weights.h"
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <vector>

using namespace godot;

// How a volume passes its voxels' materials to the shader. See
// JarVoxelVolume::MaterialMode.
enum class MaterialMode
{
    Weights = 0, // materials 0-7: 8 blend weights per vertex (COLOR + CUSTOM0)
    Split = 1,   // one material per triangle (CUSTOM0.r); vertices split at borders
    Indexed = 2, // per triangle the 4 strongest of 255 materials (CUSTOM0 ids,
                 // COLOR weights); vertices split where the id sets differ
};

// Turns per-vertex material sets into vertex attributes for a material mode.
// Split and indexed modes duplicate vertices so every triangle has uniform ids.
struct MaterialMeshBuilder
{
    static constexpr uint8_t Ground = 255; // split mode: no painted material

    // In: positions, normals, indices and per-vertex materials. Out: arrays in
    // place, plus COLOR, CUSTOM0 (RGBA8) and per-vertex painted amount (for
    // population: nothing grows on painted ground).
    static void build(MaterialMode mode, PackedVector3Array &verts, PackedVector3Array &normals,
                      PackedInt32Array &indices, const std::vector<VertexMaterials> &materials,
                      PackedColorArray &colors, PackedByteArray &custom0, PackedFloat32Array &painted);
};

#endif // MATERIAL_MESH_BUILDER_H
