#ifndef STITCHED_SURFACE_NETS_H
#define STITCHED_SURFACE_NETS_H

#include "chunk_mesh_data.h"
#include "material_mesh_builder.h"
#include "stitched_mesh_chunk.h"
#include "mesh_compute_scheduler.h"
#include "voxel_lod.h"
#include "voxel_octree_node.h"
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <array>
#include <vector>

using namespace godot;

class JarVoxelVolume;

class StitchedSurfaceNets
{
  private:
    PackedVector3Array _verts;
    PackedVector3Array _normals;
    // Materials per vertex; turned into vertex attributes per material mode at
    // the end (MaterialMeshBuilder).
    std::vector<VertexMaterials> _vertexMaterials;
    PackedInt32Array _indices;
    std::vector<bool> _badNormals;
    std::unordered_map<glm::ivec3, int> _innerEdgeNodes;
    std::unordered_map<glm::ivec3, int> _ringEdgeNodes;

    const VoxelOctreeNode *_chunk;
    const bool _cubicVoxels;
    const bool _dualContouring;
    const bool _manifold;
    const float _featureThreshold;
    const MaterialMode _materialMode;

    // Manifold mode: a cell can hold several separate pieces of surface (thin
    // walls, nearby blobs). Each piece gets its own vertex; for every cell edge
    // this stores which piece it belongs to (NoComponent if not crossed). The
    // piece's vertex is vertexIndices[node] + component.
    static constexpr uint8_t NoComponent = 0xFF;
    std::vector<std::array<uint8_t, 12>> _edgeComponent;
    // Groups the cell's crossed edges into connected pieces; returns the count.
    int find_components(const std::vector<int> &neighbours, std::array<uint8_t, 12> &edgeComponent) const;
    void create_component_vertices(const int node_id, const std::vector<int> &neighbours);
    static int quad_edge(int face, int k);
    void register_edge_node(const int node_id, const std::vector<int> &neighbours, const bool on_ring);
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
    glm::vec3 place_dual_contouring_vertex(const int node_id, const std::vector<int> &neighbours, glm::vec3 &normal,
                                           uint16_t edgeMask = 0x0FFF);
    // Dual contouring: splits quads along the diagonal that best matches the
    // vertex normals, and gives sharp creases their own normals.
    void add_quad(int n0, int n1, int n2, int n3, bool flip);
    // Per vertex: placed on a sharp edge or corner (only those get crease
    // normals; smooth curvature keeps smooth shading). Set by
    // place_dual_contouring_vertex for the vertex being created.
    std::vector<uint8_t> _sharp;
    bool _lastVertexSharp = false;
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
