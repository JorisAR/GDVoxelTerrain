#ifndef STITCHED_MESH_CHUNK_H
#define STITCHED_MESH_CHUNK_H

#include "voxel_octree_node.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

class JarVoxelTerrain;

class StitchedMeshChunk
{
  public:
    static const std::vector<Bounds> RingBounds;
    static const std::vector<glm::vec3> CheckLodOffsets;
    static const std::vector<glm::ivec4> RingQuadChecks;
    static const std::vector<glm::ivec3> Offsets;
    static const std::vector<glm::ivec2> Edges;
    static const std::vector<glm::ivec3> YzOffsets;
    static const std::vector<glm::ivec3> XzOffsets;
    static const std::vector<glm::ivec3> XyOffsets;
    static const std::vector<std::vector<glm::ivec3>> FaceOffsets;

    bool should_have_quad(const glm::ivec3 &position, const int face) const;
    bool on_positive_edge(const glm::ivec3 &position) const;
    int get_node_index_at(const glm::ivec3 &pos) const;
    bool get_unique_neighbouring_vertices(const glm::ivec3 &pos, const std::vector<glm::ivec3> &offsets,
                                          std::vector<int> &result) const;

    bool get_neighbours(const glm::ivec3 &pos, std::vector<int> &result) const;
    bool get_ring_neighbours(const glm::ivec3 &pos, std::vector<int> &result) const;
    bool should_have_boundary_quad(const std::vector<int> &neighbours, const bool on_ring) const;

    StitchedMeshChunk(const JarVoxelTerrain &terrain, const VoxelOctreeNode &chunk);

    bool is_edge_chunk() const
    {
        return _lodH2LBoundaries != 0 || _lodL2HBoundaries != 0;
    }

    bool is_on_any_boundary(const glm::ivec3 &position) const
    {
        // return is_on_boundary(_lodL2HBoundaries, position);
        // // return is_on_boundary(_lodL2HBoundaries | _lodH2LBoundaries, position);
        // return false;
        return _lodH2LBoundaries != 0 && (position.x == LargestPos - 2 && (_lodH2LBoundaries & 0b1) > 0 ||
                                          position.x == 1 && (_lodH2LBoundaries & 0b10) > 0 ||
                                          position.y == LargestPos - 2 && (_lodH2LBoundaries & 0b100) > 0 ||
                                          position.y == 1 && (_lodH2LBoundaries & 0b1000) > 0 ||
                                          position.z == LargestPos - 2 && (_lodH2LBoundaries & 0b10000) > 0 ||
                                          position.z == 1 && (_lodH2LBoundaries & 0b100000) > 0);
    }

    bool is_on_boundary(const uint8_t boundaries, const glm::ivec3 &position) const
    {
        return boundaries != 0 &&
               (position.x == LargestPos && (boundaries & 0b1) > 0 || position.x == 0 && (boundaries & 0b10) > 0 ||
                position.y == LargestPos && (boundaries & 0b100) > 0 || position.y == 0 && (boundaries & 0b1000) > 0 ||
                position.z == LargestPos && (boundaries & 0b10000) > 0 ||
                position.z == 0 && (boundaries & 0b100000) > 0);
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

    glm::vec3 get_half_leaf_size() const
    {
        return half_leaf_size;
    }

    glm::ivec3 Octant{1, 1, 1};
    std::vector<VoxelOctreeNode *> nodes;
    std::vector<glm::ivec3> positions;
    std::vector<int> vertexIndices;
    std::vector<int> faceDirs;
    int innerNodeCount = 0;
    int ringNodeCount = 0;
    std::unordered_map<glm::ivec3, int> _ringLut; //maps position to index in ringNodes

    uint8_t _lodL2HBoundaries; // boundaries from low lod to high lod, i.e. large to small chunks
    uint8_t _lodH2LBoundaries; // boundaries from high lod to low lod, i.e. small to large chunks


  private:
    glm::vec3 half_leaf_size;
    const static int ChunkRes = 16 + 2;
    const static int LargestPos = ChunkRes - 1;
    std::vector<int> _leavesLut; //maps position to index in nodes
    // chunk boundaries, 6 bits each: 0,0,-z,z,-y,y,-x,x

};

#endif // MESH_CHUNK_H
