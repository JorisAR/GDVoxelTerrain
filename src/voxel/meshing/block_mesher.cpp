#include "block_mesher.h"
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <memory>

namespace
{
// Whether the tile's cells change sign anywhere (else it holds no surface).
bool has_surface(const VoxelBlock &block, const glm::ivec3 &firstCell, int cells)
{
    bool inside = false, outside = false;
    for (int z = 0; z < cells; ++z)
        for (int y = 0; y < cells; ++y)
            for (int x = 0; x < cells; ++x)
            {
                (block.value_at(firstCell + glm::ivec3(x, y, z)) < 0.0f ? inside : outside) = true;
                if (inside && outside)
                    return true;
            }
    return false;
}

template <typename T> void append(Array &into, const Array &from, int slot)
{
    T joined = into[slot];
    joined.append_array(T(from[slot]));
    into[slot] = joined;
}
} // namespace

bool BlockMesher::mesh(const VoxelBlock &block, const MesherSettings &settings, Array &meshData,
                       PackedFloat32Array &painted)
{
    constexpr int Tile = StitchedMeshChunk::TileCells;
    const glm::ivec3 tiles = (block.size + Tile - 1) / Tile;
    std::vector<std::unique_ptr<VoxelOctreeNode>> storage;
    bool any = false;
    for (int tz = 0; tz < tiles.z; ++tz)
        for (int ty = 0; ty < tiles.y; ++ty)
            for (int tx = 0; tx < tiles.x; ++tx)
            {
                // A tile's own cells start at 16 * t; its samples one cell before.
                const glm::ivec3 firstCell = glm::ivec3(tx, ty, tz) * Tile - 1;
                if (!has_surface(block, firstCell, Tile + 2))
                    continue;
                StitchedSurfaceNets mesher(settings, StitchedMeshChunk(block, firstCell, storage), block.origin);
                Array tileData;
                PackedFloat32Array tilePainted;
                if (!mesher.generate_arrays(tileData, tilePainted))
                    continue;
                if (!any)
                {
                    meshData = tileData;
                    painted = tilePainted;
                    any = true;
                    continue;
                }
                // Join: indices shift by the vertices before them.
                const int32_t offset = int32_t(PackedVector3Array(meshData[Mesh::ARRAY_VERTEX]).size());
                PackedInt32Array indices = tileData[Mesh::ARRAY_INDEX];
                int32_t *w = indices.ptrw();
                for (int64_t k = 0; k < indices.size(); ++k)
                    w[k] += offset;
                tileData[Mesh::ARRAY_INDEX] = indices;
                append<PackedVector3Array>(meshData, tileData, Mesh::ARRAY_VERTEX);
                append<PackedVector3Array>(meshData, tileData, Mesh::ARRAY_NORMAL);
                append<PackedColorArray>(meshData, tileData, Mesh::ARRAY_COLOR);
                append<PackedByteArray>(meshData, tileData, Mesh::ARRAY_CUSTOM0);
                append<PackedInt32Array>(meshData, tileData, Mesh::ARRAY_INDEX);
                painted.append_array(tilePainted);
            }
    return any;
}
