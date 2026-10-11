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

class JarVoxelVolume;

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
    const bool _dualContouring;
    const float _featureThreshold;
    StitchedMeshChunk _meshChunk;

    // Dual contouring: surface normal per node (stored by edits, otherwise the
    // volume SDF's analytic gradient), computed on demand and cached.
    const JarSignedDistanceField *_sdf;
    std::vector<glm::vec3> _gradients;
    std::vector<float> _distanceScale; // 1 / |gradient|: turns sample values into true distances
    std::vector<bool> _hasGradient;
    glm::vec3 gradient_at(int node_id);
    // Returns NaN if no dual contouring position fits the cell. `normal` gets the
    // average surface normal of the cell's samples (exact, for shading).
    glm::vec3 place_dual_contouring_vertex(const int node_id, const std::vector<int> &neighbours, glm::vec3 &normal);
    // Dual contouring: splits quads along the diagonal that best matches the
    // vertex normals, and gives sharp creases their own normals.
    void add_quad(int n0, int n1, int n2, int n3, bool flip);
    void split_creases();

    void add_tri(int n0, int n1, int n2, bool flip);
    void add_tri_fix_normal(int n0, int n1, int n2);
    void create_vertex(const int node_id, const std::vector<int> &neighbours, const bool on_ring);
    std::vector<std::vector<int>> find_ring_nodes(const glm::ivec3 &pos, const int face) const;    

  public:
    StitchedSurfaceNets(const JarVoxelVolume &terrain, const VoxelOctreeNode &chunk);
    ChunkMeshData *generate_mesh_data(const JarVoxelVolume &terrain);
};

#endif // SURFACE_NETS_H
