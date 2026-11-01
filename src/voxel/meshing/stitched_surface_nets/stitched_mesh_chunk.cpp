#include "stitched_mesh_chunk.h"
#include "voxel_block.h"
#include "voxel_volume.h"
#include <limits>

const std::vector<glm::ivec2> StitchedMeshChunk::Edges = {
    glm::ivec2(0, 1), glm::ivec2(2, 3), glm::ivec2(4, 5), glm::ivec2(6, 7), glm::ivec2(0, 2), glm::ivec2(1, 3),
    glm::ivec2(4, 6), glm::ivec2(5, 7), glm::ivec2(0, 4), glm::ivec2(1, 5), glm::ivec2(2, 6), glm::ivec2(3, 7)};

StitchedMeshChunk::StitchedMeshChunk(const JarVoxelVolume &terrain, const VoxelOctreeNode &chunk)
{
    const float voxel = terrain.get_voxel_size();
    const Bounds bounds = chunk.get_bounds(voxel);
    // Every leaf touching the chunk, each at its own region's LOD: the chunk's
    // own and its neighbors' (the vertices on the chunk's border need theirs).
    terrain.get_voxel_leaves_in_bounds(bounds.expanded(0.01f * voxel), nodes);
    if (nodes.empty())
        return;
    boundaries = chunk.compute_boundaries(terrain);

    // The lattice: cells of the smallest leaf, one layer of them around the
    // chunk. Leaves are on the voxel grid, so all of this is exact.
    float unit = std::numeric_limits<float>::max();
    for (const VoxelOctreeNode *node : nodes)
        unit = std::min(unit, node->edge_length(voxel));
    const glm::vec3 origin = bounds.min - glm::vec3(unit);
    dims = glm::ivec3(glm::round(bounds.get_size() / unit)) + 2;
    _lattice.assign(size_t(dims.x) * dims.y * dims.z, -1);
    minCell.resize(nodes.size());
    cells.resize(nodes.size());
    own.resize(nodes.size());
    for (int i = 0; i < int(nodes.size()); ++i)
    {
        const VoxelOctreeNode *node = nodes[i];
        const float edge = node->edge_length(voxel);
        minCell[i] = glm::ivec3(glm::round((node->_center - glm::vec3(0.5f * edge) - origin) / unit));
        cells[i] = int(std::lround(edge / unit));
        const glm::vec3 c = node->_center;
        own[i] = c.x > bounds.min.x && c.x < bounds.max.x && c.y > bounds.min.y && c.y < bounds.max.y &&
                 c.z > bounds.min.z && c.z < bounds.max.z;
        fill(i);
    }
}

StitchedMeshChunk::StitchedMeshChunk(const VoxelBlock &block, const glm::ivec3 &firstCell,
                                     std::vector<std::unique_ptr<VoxelOctreeNode>> &storage)
{
    constexpr int Res = TileCells + 2;
    dims = glm::ivec3(Res);
    _lattice.assign(size_t(Res) * Res * Res, -1);
    storage.clear();
    storage.reserve(Res * Res * Res);
    for (int z = 0; z < Res; ++z)
        for (int y = 0; y < Res; ++y)
            for (int x = 0; x < Res; ++x)
            {
                const glm::ivec3 local(x, y, z);
                const glm::ivec3 cell = firstCell + local;
                storage.push_back(std::make_unique<VoxelOctreeNode>(nullptr, block.position(cell), 0));
                // Every sample carries a normal: the mesher needs no SDF then.
                storage.back()->set_sample(block.value_at(cell), block.materials_at(cell),
                                           Octahedral::encode(block.normal_at(cell)));
                nodes.push_back(storage.back().get());
                minCell.push_back(local);
                cells.push_back(1);
                own.push_back(glm::all(glm::greaterThanEqual(local, glm::ivec3(1))) &&
                              glm::all(glm::lessThanEqual(local, glm::ivec3(TileCells))));
                fill(int(nodes.size()) - 1);
            }
}

void StitchedMeshChunk::fill(int sample)
{
    const glm::ivec3 lo = glm::max(minCell[sample], glm::ivec3(0));
    const glm::ivec3 hi = glm::min(minCell[sample] + cells[sample], dims);
    for (int z = lo.z; z < hi.z; ++z)
        for (int y = lo.y; y < hi.y; ++y)
            for (int x = lo.x; x < hi.x; ++x)
                _lattice[x + dims.x * (y + dims.y * z)] = sample;
}
