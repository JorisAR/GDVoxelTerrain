#ifndef JAR_BLOCK_MESHER_H
#define JAR_BLOCK_MESHER_H

#include "stitched_surface_nets/stitched_surface_nets.h"
#include "voxel_block.h"
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>

using namespace godot;

// Meshes a voxel block with the chunk mesher: in tiles of 16 cells laid out
// like chunks of one LOD, so neighbouring tiles share their border vertices
// exactly, joined into one surface. Vertices are relative to the block's
// origin. Thread-safe (works on the block only).
namespace BlockMesher
{
// Mesh arrays (Mesh::ARRAY_*; CUSTOM1 left to the caller) and painted amount
// per vertex; false if the block holds no surface.
bool mesh(const VoxelBlock &block, const MesherSettings &settings, Array &meshData, PackedFloat32Array &painted);
} // namespace BlockMesher

#endif // JAR_BLOCK_MESHER_H
