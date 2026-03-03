#ifndef VOXEL_OCTREE_NODE_H
#define VOXEL_OCTREE_NODE_H

#include "bounds.h"
#include "voxel_chunk.h"
#include "sdf_modification.h"
#include "octree_node.h"
#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace godot {

  class JarVoxelTerrain;

  class VoxelOctreeNode : public OctreeNode<VoxelOctreeNode>
  {
  private:
    float _value = 0;
    int LoD = 0;
    bool _isGenerated = false; // if true, already generated from SDF.
    bool _isModified = false; // if true, modified, so should not be deleted.
    bool _isDirty = false; // if true, needs to recalculate value from children
    bool _isEnqueued = false; // if true, is enqueued for meshing
    uint8_t _isMaterialized;

    JarVoxelChunk *_chunk = nullptr;

    uint32_t _material_index = 0;
    glm::vec3 _normal{0, 0, 0};

    bool is_dirty() const;
    void set_dirty(bool value);
    void set_value(float value, glm::vec3 normal, int material_index);

    // idea to not explore the whole tree, but only the children that are not materialized
    void mark_materialized();
    bool is_materialized();

    bool is_one_above_chunk(const JarVoxelTerrain &terrain) const;
    void populateUniqueLoDValues(std::vector<int> &lodValues) const;

    bool should_delete_chunk(const JarVoxelTerrain &terrain) const;

    void update_if_dirty();

  public:
    VoxelOctreeNode(int size);
    VoxelOctreeNode(VoxelOctreeNode *parent, const glm::vec3 center, int size);

    int priority() const;

    uint16_t compute_boundaries(const JarVoxelTerrain &terrain) const;

    JarVoxelChunk *get_chunk() const;
    bool is_chunk(const JarVoxelTerrain &terrain) const;
    bool is_above_chunk(const JarVoxelTerrain &terrain) const;
    bool is_above_min_chunk(const JarVoxelTerrain &terrain) const;
    bool is_enqueued() const;
    void finished_meshing_notify_parent_and_children() const;
    bool is_parent_enqueued() const;
    bool is_any_children_enqueued() const;

    void build(JarVoxelTerrain &terrain);

    inline bool has_surface(const JarVoxelTerrain &terrain, const float value);
    void queue_update(JarVoxelTerrain &terrain);
    void modify_sdf_in_bounds(JarVoxelTerrain &terrain, const ModifySettings &sdf);
    void update_chunk(JarVoxelTerrain &terrain, ExtractedMeshData *chunkMeshData);

    void delete_chunk();
    void get_voxel_leaves_in_bounds(const JarVoxelTerrain &terrain, const Bounds &Bounds,
                                    std::vector<VoxelOctreeNode *> &result);
    void get_voxel_leaves_in_bounds(const JarVoxelTerrain &terrain, const Bounds &Bounds, const int LOD,
                                    std::vector<VoxelOctreeNode *> &result);

    void get_voxel_leaves_in_bounds_excluding_bounds(const JarVoxelTerrain &terrain, const Bounds &including_bounds,
                                                     const Bounds &excluding_bounds, const int LOD,
                                                     std::vector<VoxelOctreeNode *> &result);

    bool is_modified() const;
    uint32_t get_lod() const;
    float get_value();
    glm::vec3 get_normal();
    uint32_t get_material_index();

    // private:

  protected:
    inline virtual std::unique_ptr<VoxelOctreeNode> create_child_node(const glm::vec3 &center, uint32_t size) override;
  };
}

#endif // VOXEL_OCTREE_NODE_H
