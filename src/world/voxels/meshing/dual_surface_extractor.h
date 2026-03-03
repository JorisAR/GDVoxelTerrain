#ifndef STITCHED_SURFACE_NETS_H
#define STITCHED_SURFACE_NETS_H

#include "mesh_extractor.h"
#include "mesh_surface.h"
#include "mesh_chunk.h"
#include <memory>

namespace godot
{

  // Implementation of the Surface Nets (a dual surface extraction method) algorithm for mesh extraction

  class DualSurfaceExtractor : public MeshExtractor
  {
  public:
    DualSurfaceExtractor(const JarVoxelTerrain &terrain);

    ExtractedMeshData *generate_mesh_data(const JarVoxelTerrain &terrain, const VoxelOctreeNode &chunk) override;

  private:
    bool _split_by_material = false;
    bool _cubicVoxels = false;
    VoxelOctreeNode *_chunk;
    std::unique_ptr<MeshChunk> _meshChunk;

    MeshSurface _mainSurface;
    std::unordered_map<uint32_t, MeshSurface> _surfaces;

    // for LOD stitching
    std::unordered_map<glm::ivec3, int> _innerEdgeNodes;
    std::unordered_map<glm::ivec3, int> _ringEdgeNodes;

    void create_vertex(const JarVoxelTerrain &terrain, const int node_id, const std::vector<int> &neighbours, bool on_ring);

    std::vector<std::vector<int>> find_ring_nodes(const glm::ivec3 &pos, const int face) const;

    void stitch_inner_faces();

    void stitch_edge_chunk_boundaries();

    ExtractedMeshData *build_chunk_mesh_data(const JarVoxelTerrain &terrain) const;

    void add_triangle_to_surface(int n0, int n1, int n2, WindingPolicy policy, uint32_t material_index);
    void add_quad_to_surface(int n0, int n1, int n2, int n3, WindingPolicy policy, uint32_t material_index);

    MeshSurface &get_surface_for_material(uint32_t material_index);
  };
} // namespace godot

#endif // STITCHED_SURFACE_NETS_H
