#ifndef STITCHED_MESH_CHUNK_H
#define STITCHED_MESH_CHUNK_H
#include "voxel_octree_node.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class JarVoxelVolume;
struct VoxelBlock;

// What a chunk is meshed from: the samples (octree leaves at their region's
// LOD) in and around the chunk, on a lattice of the smallest of them.
//
// Samples are leaf centers; the mesh is dual to them (surface nets / dual
// contouring), generalized to leaves of mixed sizes, as where LODs meet:
// - a vertex for each point where leaves meet (an octree vertex), from the 8
//   leaves around it (a larger leaf may fill several of the 8 places);
// - a quad for each face between two leaves whose samples differ in sign,
//   between the vertices at the face's 4 corners. Where a large leaf meets
//   smaller ones, the faces are the smaller leaves'.
// A vertex depends only on the leaves around it, so every chunk that needs
// it computes the same one, and each face is meshed by exactly one chunk
// (the one owning its smaller leaf; for equal leaves, the one on the face's
// negative side): chunks of any LODs meet without cracks or overlaps.
class StitchedMeshChunk
{
  public:
    // Cube corners are numbered x + 2y + 4z; the 12 edges between them
    // (0-3 along x, 4-7 along y, 8-11 along z).
    static const std::vector<glm::ivec2> Edges;
    static constexpr int TileCells = 16; // own cells per tile and axis

    // An octree chunk with its neighbors' leaves around it.
    StitchedMeshChunk(const JarVoxelVolume &terrain, const VoxelOctreeNode &chunk);
    // A tile of a voxel block: cells firstCell + 1 .. firstCell + 16 are its
    // own, the layer around them is shared with the neighboring tiles. No
    // LODs. The samples live in `storage`.
    StitchedMeshChunk(const VoxelBlock &block, const glm::ivec3 &firstCell,
                      std::vector<std::unique_ptr<VoxelOctreeNode>> &storage);

    std::vector<VoxelOctreeNode *> nodes; // the samples
    std::vector<glm::ivec3> minCell;      // per sample: its lowest lattice cell
    std::vector<int> cells;               // per sample: its edge, in lattice cells
    std::vector<uint8_t> own;             // per sample: this chunk meshes its faces
    glm::ivec3 dims{0};                   // lattice size, in cells

    // The sample filling lattice cell `c`, -1 for none (outside the lattice
    // or the volume).
    int sample_at(const glm::ivec3 &c) const
    {
        if (glm::any(glm::lessThan(c, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(c, dims)))
            return -1;
        return _lattice[c.x + dims.x * (c.y + dims.y * c.z)];
    }

    // Whether lattice point `p` is a corner of one of the samples around it
    // (an octree vertex: where a dual cell, and a vertex, can be).
    bool is_corner(const glm::ivec3 &p) const
    {
        for (int o = 0; o < 8; ++o)
        {
            const int s = sample_at(p - glm::ivec3((o & 1) ? 0 : 1, (o & 2) ? 0 : 1, (o & 4) ? 0 : 1));
            if (s < 0)
                continue;
            const glm::ivec3 lo = minCell[s], hi = minCell[s] + cells[s];
            if ((p.x == lo.x || p.x == hi.x) && (p.y == lo.y || p.y == hi.y) && (p.z == lo.z || p.z == hi.z))
                return true;
        }
        return false;
    }

    // The LODs of the 26 neighbors relative to the chunk's (see
    // VoxelOctreeNode::compute_boundaries): the mesh depends on them.
    uint64_t boundaries = 0;
    bool is_edge_chunk() const
    {
        return boundaries != 0;
    }

  private:
    std::vector<int> _lattice;
    void fill(int sample);
};

#endif // STITCHED_MESH_CHUNK_H
