#ifndef VOXEL_OCTREE_NODE_H
#define VOXEL_OCTREE_NODE_H

#include "bounds.h"
#include "voxel_chunk.h"
#include "modify_settings.h"
#include "octree_node.h"
#include <algorithm>
#include <array>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class JarVoxelTerrain;

class VoxelOctreeNode : public OctreeNode<VoxelOctreeNode>
{
  private:
    float _value = 0;
    glm::vec4 NodeColor{0, 0, 0, 0};

    bool _isSet = false;
    bool _isDirty = false;
    bool _isEnqueued = false;
    uint8_t _isMaterialized = 0;

    JarVoxelChunk *_chunk = nullptr;

    int LoD = 0;

    bool is_dirty() const;
    void set_dirty(bool value);
    void set_value(float value);

    // idea to not explore the whole tree, but only the children that are not materialized
    void mark_materialized();
    bool is_materialized();

    bool is_one_above_chunk(const JarVoxelTerrain &terrain) const;
    void populateUniqueLoDValues(std::vector<int> &lodValues) const;

    bool should_delete_chunk(const JarVoxelTerrain &terrain) const;

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

    bool has_surface(const JarVoxelTerrain &terrain, const float value);
    void queue_update(JarVoxelTerrain &terrain);
    void modify_sdf_in_bounds(JarVoxelTerrain &terrain, const ModifySettings &settings);
    void update_chunk(JarVoxelTerrain &terrain, ChunkMeshData *chunkMeshData);

    void delete_chunk();
    void get_voxel_leaves_in_bounds(const JarVoxelTerrain &terrain, const Bounds &Bounds,
                                    std::vector<VoxelOctreeNode *> &result);
    void get_voxel_leaves_in_bounds(const JarVoxelTerrain &terrain, const Bounds &Bounds, const int LOD,
                                    std::vector<VoxelOctreeNode *> &result);

    void get_voxel_leaves_in_bounds_excluding_bounds(const JarVoxelTerrain &terrain, const Bounds &including_bounds,
                                                     const Bounds &excluding_bounds, const int LOD,
                                                     std::vector<VoxelOctreeNode *> &result);

    float get_value();
    int get_lod() const;
    glm::vec4 get_color() const;

    // private:

  protected:
    virtual std::unique_ptr<VoxelOctreeNode> create_child_node(const glm::vec3 &center, int size) override;
};

#endif // VOXEL_OCTREE_NODE_H
