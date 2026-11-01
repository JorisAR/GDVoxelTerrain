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
class JarBiomeMap;
struct UpField;

// How to mesh: what StitchedSurfaceNets needs besides the samples. From a
// volume's settings (for its chunks) or chosen by the caller (voxel blocks).
struct MesherSettings
{
    bool cubicVoxels = false;
    bool dualContouring = false;
    bool manifold = false;
    float featureThreshold = 0.1f;
    MaterialMode materialMode = MaterialMode(0);
    // Normals of samples that carry none; may be null when they all do.
    const JarSignedDistanceField *sdf = nullptr;
    float voxelSize = 0.0f; // for the sdf's detail per node (0: full detail)
    static MesherSettings from(const JarVoxelVolume &terrain);
};

// Meshes a grid of samples: an octree chunk (with stitched LOD seams) or a
// tile of a voxel block (StitchedMeshChunk's two constructors).
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

    const VoxelOctreeNode *_chunk = nullptr; // the octree chunk, if meshing one
    const glm::vec3 _origin;                  // vertices are relative to it
    // A sample's place relative to _origin (exact: both are on the voxel grid).
    glm::vec3 local(const VoxelOctreeNode *node) const { return node->_center - _origin; }
    // Vertices are computed relative to their cell's corner `base` (the
    // smallest sample position, the same in every chunk that has the cell),
    // so neighboring chunks compute their shared vertices from the same small
    // numbers, to the same floats. Snapped to 1/4096 m, they then add to the
    // chunk's offset, and on the GPU to the chunk's position, exactly within
    // 2 km of the scene origin (24 bits), which the floating origin keeps the
    // camera in: the seams there are watertight bit for bit. In volume
    // coordinates the vertex fit lost centimeters on large planets (a float
    // step is 6 cm at 600 km) and cracked the seams.
    static glm::vec3 cell_base(const VoxelOctreeNode *a, const VoxelOctreeNode *b) { return glm::min(a->_center, b->_center); }
    glm::vec3 from_cell(const glm::vec3 &position, const glm::vec3 &base) const
    {
        return glm::round(position * 4096.0f) * (1.0f / 4096.0f) + (base - _origin);
    }
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
    float _voxelSize;
    std::vector<glm::vec3> _gradients;
    std::vector<float> _distanceScale; // 1 / |gradient|: turns sample values into true distances
    std::vector<bool> _hasGradient;
    glm::vec3 gradient_at(int node_id);
    glm::vec3 project_to_volume_sdf(glm::vec3 point, glm::vec3 &normal) const;
    // Returns NaN if no dual contouring position fits the cell. `normal` gets the
    // average surface normal of the cell's samples (exact, for shading).
    glm::vec3 place_dual_contouring_vertex(const int node_id, const std::vector<int> &neighbours, glm::vec3 &normal,
                                           uint16_t edgeMask = 0x0FFF);
    // Quad dual to the grid edge from sample `a` to sample `b`. Dual
    // contouring splits it along the diagonal that best matches the vertex
    // normals. A quad whose triangles would both face against the edge (folded:
    // placed vertices crossed over, the jagged teeth along sharp ridges) is fanned
    // around the edge's surface crossing instead; its outline stays, so the
    // neighbouring quads (and chunks) are unaffected.
    void add_quad(int n0, int n1, int n2, int n3, bool flip, int a, int b);
    int add_crossing_vertex(int a, int b);
    // Per vertex: placed on a sharp edge or corner (only those get crease
    // normals; smooth curvature keeps smooth shading). Set by
    // place_dual_contouring_vertex for the vertex being created.
    std::vector<uint8_t> _sharp;
    bool _lastVertexSharp = false;
    // Surface directions meeting at a sharp vertex: its cell's sample normals,
    // clustered. Faces there take the closest one, which only depends on the
    // cell, so chunks sharing the vertex shade it alike.
    struct SharpPlanes
    {
        uint8_t count = 0;
        glm::vec3 normals[4];
    };
    std::vector<SharpPlanes> _sharpPlanes; // per vertex, empty unless sharp
    SharpPlanes _lastPlanes;
    void split_creases();

    void add_tri(int n0, int n1, int n2, bool flip);
    void add_tri_fix_normal(int n0, int n1, int n2);
    void create_vertex(const int node_id, const std::vector<int> &neighbours, const bool on_ring);
    std::vector<std::vector<int>> find_ring_nodes(const glm::ivec3 &pos, const int face) const;    

  public:
    StitchedSurfaceNets(const JarVoxelVolume &terrain, const VoxelOctreeNode &chunk);
    StitchedSurfaceNets(const MesherSettings &settings, StitchedMeshChunk &&input, const glm::vec3 &origin);
    // An octree chunk's mesh, with biome weights and chunk metadata; null if empty.
    ChunkMeshData *generate_mesh_data(const JarVoxelVolume &terrain);
    // The mesh arrays (CUSTOM1 left empty) and painted amount per vertex;
    // false if there is no surface. Runs once per instance.
    bool generate_arrays(Array &meshData, PackedFloat32Array &painted);
    // Biome weights for biomes 0-3 per vertex (CUSTOM1, RGBA8); vertices are
    // relative to `origin` in the volume.
    static PackedByteArray biome_weights(const PackedVector3Array &verts, const Ref<JarBiomeMap> &biomes,
                                         const UpField &up, const glm::vec3 &origin);
};

#endif // SURFACE_NETS_H
