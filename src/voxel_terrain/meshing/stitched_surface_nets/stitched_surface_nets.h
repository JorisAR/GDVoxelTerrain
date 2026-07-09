#ifndef STITCHED_SURFACE_NETS_H
#define STITCHED_SURFACE_NETS_H

#include "chunk_mesh_data.h"
#include "stitched_mesh_chunk.h"
#include "mesh_compute_scheduler.h"
#include "voxel_lod.h"
#include "voxel_octree_node.h"
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <vector>

using namespace godot;

class JarVoxelTerrain;

class StitchedSurfaceNets
{
  private:
    PackedVector3Array _verts;
    PackedVector3Array _normals;
    PackedColorArray _colors;
    PackedInt32Array _indices;
    std::vector<bool> _badNormals;
    std::unordered_map<glm::ivec3, int> _innerEdgeNodes;
    std::unordered_map<glm::ivec3, int> _ringEdgeNodes;

    const VoxelOctreeNode *_chunk;
    const bool _cubicVoxels;
    StitchedMeshChunk _meshChunk;

    void add_tri(int n0, int n1, int n2, bool flip);
    void add_tri_fix_normal(int n0, int n1, int n2);
    void create_vertex(const int node_id, const std::vector<int> &neighbours, const bool on_ring);
    std::vector<std::vector<int>> find_ring_nodes(const glm::ivec3 &pos, const int face) const;    

  public:
    StitchedSurfaceNets(const JarVoxelTerrain &terrain, const VoxelOctreeNode &chunk);
    ChunkMeshData *generate_mesh_data(const JarVoxelTerrain &terrain);
};

#endif // SURFACE_NETS_H
