#ifndef ADAPTIVE_MESH_CHUNK_H
#define ADAPTIVE_MESH_CHUNK_H

#include "voxel_octree_node.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class JarVoxelTerrain;

class AdaptiveMeshChunk
{
  public:
    static const std::vector<glm::ivec3> Offsets;
    static const std::vector<glm::ivec2> Edges;
    static const std::vector<glm::ivec3> YzOffsets;
    static const std::vector<glm::ivec3> XzOffsets;
    static const std::vector<glm::ivec3> XyOffsets;
    static const std::vector<std::vector<glm::ivec3>> FaceOffsets;

    bool should_have_quad(const glm::ivec3 &position, const int face, const bool isSmall) const;
    int get_node_index_at(const glm::ivec3 &pos) const;
    bool get_unique_neighbouring_vertices(const glm::ivec3 &pos, const std::vector<glm::ivec3> &offsets,
                                          std::vector<int> &result) const;

    bool get_neighbours(const glm::ivec3 &pos, std::vector<int> &result) const;

    AdaptiveMeshChunk(const JarVoxelTerrain &terrain, const VoxelOctreeNode &chunk);
    int get_chunk_resolution() const
    {
        return _chunkResolution;
    }
    bool is_edge_chunk() const
    {
        return IsEdgeChunk;
    }
    int get_real_lod() const
    {
        return RealLoD;
    }
    const std::vector<glm::ivec3> &get_positions() const
    {
        return positions;
    }
    const std::vector<int> &get_vertex_indices() const
    {
        return vertexIndices;
    }
    const std::vector<int> &get_face_dirs() const
    {
        return faceDirs;
    }

    glm::vec3 get_half_leaf_size() const {
        return half_leaf_size;
    }

    glm::ivec3 Octant{1, 1, 1};
    std::vector<VoxelOctreeNode *> nodes;
    std::vector<glm::ivec3> positions;
    std::vector<int> vertexIndices;
    std::vector<int> faceDirs;
    std::vector<bool> isSmalls;

private:
    glm::vec3 half_leaf_size;
    const static int ChunkRes = 16 + 2;
    const static int LargeChunkRes = 2 * ChunkRes;
    int _chunkResolution = ChunkRes;
    std::vector<int> _leavesLut;
    bool IsEdgeChunk = false;
    int RealLoD = 0;
};

#endif // MESH_CHUNK_H
