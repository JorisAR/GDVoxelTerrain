#ifndef VOXEL_OCTREE_NODE_H
#define VOXEL_OCTREE_NODE_H

#include "bounds.h"
#include "voxel_chunk.h"
#include "modify_settings.h"
#include "octree_node.h"
#include "material_weights.h"
#include "octahedral.h"
#include "voxel_block.h"
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
    // Fields are ordered to pack tightly: there are millions of nodes.
    JarVoxelChunk *_chunk = nullptr;
    MaterialWeights _materials; // painted material weights
    float _value = 0;
    // Surface normal of the field that produced this node's value, packed
    // (Octahedral). Set by edits; 0 means "use the volume SDF's gradient".
    // Dual contouring uses it to place vertices on sharp features.
    uint32_t _normal = 0;
    int8_t LoD = 0;
    // Written by the build worker only (builds and edits), so plain bits are safe.
    enum Flags : uint8_t
    {
        FlagGenerated = 1,    // already generated from the SDF
        FlagEdited = 2,       // this node or a descendant was changed by an edit
        FlagPrunePending = 4, // LOD got coarser: drop finer detail once meshed
    };
    uint8_t _flags = 0;
    bool has_flag(uint8_t f) const { return (_flags & f) != 0; }
    void set_flag(uint8_t f, bool on) { _flags = on ? (_flags | f) : (_flags & ~f); }
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
    std::atomic<uint8_t> _isMaterialized{0};
    uint64_t _meshedEmptyBoundaries = 0;
    // Chunks shown (_chunk) and queued (_isEnqueued) in this subtree, this
    // node included, so walks for them skip subtrees without any: committing
    // a coarse chunk walked its whole subtree (up to ~300k nodes) to find the
    // finer chunks it replaces, 25-60 ms per commit. Updated along the
    // ancestors whenever either changes (builds queue from parallel threads).
    // Only ever too high (a node freed with a chunk), which costs a longer
    // walk, never a missed chunk: the walks still check the nodes themselves.
    std::atomic<int32_t> _chunksBelow{0};
    std::atomic<int32_t> _enqueuedBelow{0};
    void set_enqueued(bool value);
    void set_chunk(JarVoxelChunk *chunk);
    void get_shown_chunks_replaced_below(std::vector<VoxelOctreeNode *> &result);
    // A chunk shown or queued among the descendants (not this node).
    bool subtree_has_chunk_below() const
    {
        if (is_leaf())
            return false;
        for (const auto &child : *_children)
            if (child._chunksBelow.load(std::memory_order_relaxed) > 0 ||
                child._enqueuedBelow.load(std::memory_order_relaxed) > 0)
                return true;
        return false;
    }

  public:
    // Mesh task, when the chunk came out empty: the LOD boundaries it was
    // meshed for (see _meshedEmpty), read when the result is applied.
    void set_empty_boundaries(uint64_t boundaries) { _meshedEmptyBoundaries = boundaries; }

  private:

    bool is_dirty() const;
    void set_dirty(bool value);
    void set_value(float value);

    // idea to not explore the whole tree, but only the children that are not materialized
    void mark_materialized();
    bool is_materialized();

    bool is_one_above_chunk(const JarVoxelVolume &terrain) const;
    void mark_edited();
    bool subtree_has_chunk() const;
    void prune_finer_than(int lod);
    void populateUniqueLoDValues(std::vector<int> &lodValues) const;

    bool should_delete_chunk(const JarVoxelVolume &terrain) const;

  public:
    VoxelOctreeNode(int size);
    VoxelOctreeNode(VoxelOctreeNode *parent, const glm::vec3 center, int size);

    int priority() const;

    uint64_t compute_boundaries(const JarVoxelVolume &terrain) const;

    JarVoxelChunk *get_chunk() const;
    bool is_chunk(const JarVoxelVolume &terrain) const;
    bool is_above_chunk(const JarVoxelVolume &terrain) const;
    bool is_above_min_chunk(const JarVoxelVolume &terrain) const;
    bool is_enqueued() const;
    void cancel_enqueue();
    void finished_meshing_notify_related() const;
    void delete_chunks_below() const;
    bool is_any_ancestor_enqueued() const;
    bool is_any_descendant_enqueued() const;

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
    bool has_normal() const { return _normal != 0; }
    glm::vec3 get_normal() const { return Octahedral::decode(_normal); }
    uint32_t get_packed_normal() const { return _normal; }
    // A standalone sample (not part of a tree), e.g. a voxel block's cell fed
    // to the mesher.
    void set_sample(float value, const MaterialWeights &materials, uint32_t normal)
    {
        _value = value;
        _materials = materials;
        _normal = normal;
        set_flag(FlagGenerated, true);
    }
    // Copies this subtree's leaves into the block's cells they cover (build
    // worker: the tree must not change meanwhile).
    void sample_into(const JarVoxelVolume &terrain, VoxelBlock &block);
    // Recomputes averaged values of dirty inner nodes in this subtree. The build
    // worker calls it after every build/edit, so mesh threads only ever read.
    void update_if_dirty();
    void build_post(JarVoxelVolume &terrain);

    bool has_surface(const JarVoxelVolume &terrain, const float value);
    // Asks for a new mesh of this chunk; an edit's (`urgent`) go first. Again
    // while queued: an edit changed it, so a mesh being made is outdated.
    void queue_update(JarVoxelVolume &terrain, bool urgent = false);
    // The chunks (nodes the size of their LOD's chunks) touching `bounds`.
    void get_chunks_touching(const JarVoxelVolume &terrain, const Bounds &bounds,
                             std::vector<VoxelOctreeNode *> &result);
    // The shown chunks this chunk replaces: coarser ones above it, finer ones below.
    void get_shown_chunks_replaced(std::vector<VoxelOctreeNode *> &result);
    // The deepest node containing `point` (volume space), null if outside this one.
    VoxelOctreeNode *node_at(float voxelSize, const glm::vec3 &point);
    // Whether an edit changed anything in the deepest node enclosing `bounds`.
    bool edited_around(float voxelSize, const Bounds &bounds);
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
    MaterialWeights get_materials();

    // private:
};

#endif // VOXEL_OCTREE_NODE_H
