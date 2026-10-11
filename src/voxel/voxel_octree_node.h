#ifndef VOXEL_OCTREE_NODE_H
#define VOXEL_OCTREE_NODE_H

#include "bounds.h"
#include "voxel_chunk.h"
#include "modify_settings.h"
#include "octree_node.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class JarVoxelVolume;

class VoxelOctreeNode : public OctreeNode<VoxelOctreeNode>
{
  private:
    float _value = 0;
    int LoD = 0;
    bool _isGenerated = false; // if true, already generated from SDF.
    // _isDirty and _isMaterialized propagate to the parent, which during a
    // parallel build is shared by subtrees running on different threads.
    std::atomic<bool> _isDirty{false}; // if true, needs to recalculate value from children
    bool _isEnqueued = false; // if true, is enqueued for meshing
    // Last mesh of this chunk had no surface, with these LOD boundaries. Skips
    // re-meshing it on every LOD rebuild (empty chunks never get a JarVoxelChunk,
    // so "no chunk" alone can't tell them apart from unmeshed ones). Neighbour
    // LOD changes refine shared border voxels, so a boundary change re-meshes.
    // Also reset by edits and when the node stops being a chunk.
    bool _meshedEmpty = false;
    uint16_t _meshedEmptyBoundaries = 0;
    std::atomic<uint8_t> _isMaterialized{0};

    JarVoxelChunk *_chunk = nullptr;   

    glm::vec4 _nodeColor{0, 0, 0, 0}; // for material

    bool is_dirty() const;
    void set_dirty(bool value);
    void set_value(float value);

    // idea to not explore the whole tree, but only the children that are not materialized
    void mark_materialized();
    bool is_materialized();

    bool is_one_above_chunk(const JarVoxelVolume &terrain) const;
    void populateUniqueLoDValues(std::vector<int> &lodValues) const;

    bool should_delete_chunk(const JarVoxelVolume &terrain) const;

  public:
    VoxelOctreeNode(int size);
    VoxelOctreeNode(VoxelOctreeNode *parent, const glm::vec3 center, int size);

    int priority() const;

    uint16_t compute_boundaries(const JarVoxelVolume &terrain) const;

    JarVoxelChunk *get_chunk() const;
    bool is_chunk(const JarVoxelVolume &terrain) const;
    bool is_above_chunk(const JarVoxelVolume &terrain) const;
    bool is_above_min_chunk(const JarVoxelVolume &terrain) const;
    bool is_enqueued() const;
    void cancel_enqueue();
    void finished_meshing_notify_parent_and_children() const;
    bool is_parent_enqueued() const;
    bool is_any_children_enqueued() const;

    // Parallel build: nodes of size forkSize hand their children to `subtrees`
    // instead of recursing; each subtree is then built on its own thread. The
    // post-recursion step of nodes >= forkSize is deferred to `deferredPost`
    // (children before parents) and run once all subtrees are done.
    struct BuildContext
    {
        int forkSize;
        std::vector<VoxelOctreeNode *> subtrees;
        std::vector<VoxelOctreeNode *> deferredPost;
    };
    void build(JarVoxelVolume &terrain, BuildContext *ctx = nullptr);
    // Recomputes averaged values of dirty inner nodes in this subtree. The build
    // worker calls it after every build/edit, so mesh threads only ever read.
    void update_if_dirty();
    void build_post(JarVoxelVolume &terrain);

    bool has_surface(const JarVoxelVolume &terrain, const float value);
    void queue_update(JarVoxelVolume &terrain);
    void modify_sdf_in_bounds(JarVoxelVolume &terrain, const ModifySettings &settings);
    void update_chunk(JarVoxelVolume &terrain, ChunkMeshData *chunkMeshData);

    void delete_chunk();
    void get_voxel_leaves_in_bounds(const JarVoxelVolume &terrain, const Bounds &Bounds,
                                    std::vector<VoxelOctreeNode *> &result);
    void get_voxel_leaves_in_bounds(const JarVoxelVolume &terrain, const Bounds &Bounds, const int LOD,
                                    std::vector<VoxelOctreeNode *> &result);

    void get_voxel_leaves_in_bounds_excluding_bounds(const JarVoxelVolume &terrain, const Bounds &including_bounds,
                                                     const Bounds &excluding_bounds, const int LOD,
                                                     std::vector<VoxelOctreeNode *> &result);

    float get_value();
    int get_lod() const;
    glm::vec4 get_color();

    // private:

  protected:
    virtual std::unique_ptr<VoxelOctreeNode> create_child_node(const glm::vec3 &center, int size) override;
};

#endif // VOXEL_OCTREE_NODE_H
