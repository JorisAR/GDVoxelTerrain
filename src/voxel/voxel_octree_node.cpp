#include "voxel_octree_node.h"
#include "voxel_stats.h"
#include "voxel_volume.h"
#include <algorithm>
#include <cmath>
#include <execution>

VoxelOctreeNode::VoxelOctreeNode(int size) : VoxelOctreeNode(nullptr, glm::vec3(0.0f), size)
{
}

VoxelOctreeNode::VoxelOctreeNode(VoxelOctreeNode *parent, const glm::vec3 center, int size)
    : OctreeNode(parent, center, size)
{
    if (_parent != nullptr)
    {
        LoD = _parent->LoD;
        _value = _parent->get_value();
        set_flag(FlagGenerated, _parent->has_flag(FlagGenerated));
        // Made from an edited leaf: what it copied exists nowhere else.
        set_flag(FlagEdited, _parent->has_flag(FlagEdited));
        _materials = _parent->_materials;
        _normal = _parent->_normal;
    }
}

int VoxelOctreeNode::priority() const
{
    return LoD;
}

bool VoxelOctreeNode::is_dirty() const
{
    return _isDirty;
}

void VoxelOctreeNode::set_dirty(bool value)
{
    if (!_isDirty && value && _parent != nullptr)
    {
        _parent->set_dirty(true);
    }
    _isDirty = value;
}

// Not thread-safe (accumulates in place). Only the build worker may trigger it
// with work to do; mesh threads see a clean tree (see JarVoxelVolume::build_worker).
void VoxelOctreeNode::update_if_dirty()
{
    if (!is_dirty())
        return;

    if (!is_leaf())
    {
        // Average over 8 children
        _value = 0.0f;
        MaterialWeights materials[8];
        int i = 0;
        for (auto &child : *_children)
        {
            _value += child.get_value();
            materials[i++] = child.get_materials();
        }
        _value *= 0.125f;
        _materials = MaterialWeights::average(materials);
    }

    _isDirty = false;
}

float VoxelOctreeNode::get_value()
{
    update_if_dirty();
    return _value;
}

MaterialWeights VoxelOctreeNode::get_materials()
{
    update_if_dirty();
    return _materials;
}


int VoxelOctreeNode::get_lod() const
{
    return LoD;
}

void VoxelOctreeNode::set_value(float value)
{
    _value = value;
    _isDirty = false;
    if (_parent != nullptr)
    {
        _parent->set_dirty(true);
    }
}

void VoxelOctreeNode::mark_materialized()
{
    if (is_materialized())
        return; // already marked
    if (is_leaf())
    {
        _isMaterialized = 0b11111111;
    }
    else
    {
        for (size_t i = 0; i < 8; ++i)
        {
            _isMaterialized |= ((*_children)[i].is_materialized() ? 1 : 0) << i;
        }
    }

    if (_parent != nullptr && is_materialized())
    {
        _parent->mark_materialized();
    }
}

bool VoxelOctreeNode::is_materialized()
{
    // return false;
    return _isMaterialized == 0b11111111;
}

JarVoxelChunk *VoxelOctreeNode::get_chunk() const
{
    return _chunk;
}

bool VoxelOctreeNode::is_chunk(const JarVoxelVolume &terrain) const
{
    return _size == (LoD + terrain.get_min_chunk_size());
}

bool VoxelOctreeNode::is_above_chunk(const JarVoxelVolume &terrain) const
{
    return _size > (LoD + terrain.get_min_chunk_size());
}

bool VoxelOctreeNode::is_above_min_chunk(const JarVoxelVolume &terrain) const
{
    return _size > (terrain.get_min_chunk_size());
}

bool VoxelOctreeNode::is_one_above_chunk(const JarVoxelVolume &terrain) const
{
    return _size == (LoD + terrain.get_min_chunk_size() + 1);
}

void VoxelOctreeNode::populateUniqueLoDValues(std::vector<int> &lodValues) const
{
    if (std::find(lodValues.begin(), lodValues.end(), LoD) == lodValues.end())
    {
        lodValues.push_back(LoD);
    }
    if (is_leaf())
        return;
    for (const auto &child : *_children)
    {
        child.populateUniqueLoDValues(lodValues);
    }
}

bool VoxelOctreeNode::is_enqueued() const
{
    return _isEnqueued;
}

// Called when a queued mesh task is dropped (node stopped being a chunk). A node
// left flagged as enqueued blocks delete_chunk() on the chunks it would replace
// forever, leaving stale chunks (and their details) in the scene; those get
// another chance to go now.
void VoxelOctreeNode::cancel_enqueue()
{
    set_enqueued(false);
    finished_meshing_notify_related();
}

// A chunk may go once the chunks replacing it are on screen: those are its
// descendants (finer LOD) or an ancestor (coarser), any number of levels away
// when the viewer moved fast. Retry deleting every chunk that this node's new
// mesh may have been the last missing piece for.
void VoxelOctreeNode::finished_meshing_notify_related() const
{
    for (VoxelOctreeNode *ancestor = _parent; ancestor != nullptr; ancestor = ancestor->_parent)
        ancestor->delete_chunk();
    delete_chunks_below();
}

void VoxelOctreeNode::delete_chunks_below() const
{
    if (is_leaf() || _size <= JarVoxelVolume::ChunkSizeLog2)
        return; // nothing below can be a chunk
    for (auto &child : *_children)
    {
        if (child._chunksBelow.load(std::memory_order_relaxed) == 0)
            continue;
        child.delete_chunk();
        child.delete_chunks_below();
    }
}

bool VoxelOctreeNode::is_any_ancestor_enqueued() const
{
    for (const VoxelOctreeNode *ancestor = _parent; ancestor != nullptr; ancestor = ancestor->_parent)
        if (ancestor->_isEnqueued)
            return true;
    return false;
}

bool VoxelOctreeNode::is_any_descendant_enqueued() const
{
    if (is_leaf() || _size <= JarVoxelVolume::ChunkSizeLog2)
        return false;
    for (const auto &child : *_children)
        if (child._enqueuedBelow.load(std::memory_order_relaxed) > 0 &&
            (child._isEnqueued || child.is_any_descendant_enqueued()))
            return true;
    return false;
}

bool VoxelOctreeNode::should_delete_chunk(const JarVoxelVolume &terrain) const
{
    return false;
}

// The LODs of the 26 neighbors relative to this chunk's: its mesh depends on
// their leaves (vertices on its border), so it is meshed again when these
// change. Faces (+x -x +y -y +z -z): bit i coarser, bit i + 8 finer; edges
// and corners: bits 16 + 2j coarser, 17 + 2j finer.
uint64_t VoxelOctreeNode::compute_boundaries(const JarVoxelVolume &terrain) const
{
    static const glm::vec3 faces[6] = {glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0),
                                       glm::vec3(0, -1, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)};
    uint64_t boundaries = 0;
    const float el = edge_length(terrain.get_voxel_size());
    for (int i = 0; i < 6; ++i)
    {
        const int l = terrain.lod_at(_center + el * faces[i]);
        boundaries |= uint64_t(LoD < l ? 1 : 0) << i;       // high to low
        boundaries |= uint64_t(LoD > l ? 1 : 0) << (i + 8); // low to high
    }
    int j = 0;
    for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x)
            {
                if (std::abs(x) + std::abs(y) + std::abs(z) < 2)
                    continue; // the chunk itself and its faces
                const int l = terrain.lod_at(_center + el * glm::vec3(x, y, z));
                boundaries |= uint64_t(LoD < l ? 1 : 0) << (16 + 2 * j);
                boundaries |= uint64_t(LoD > l ? 1 : 0) << (17 + 2 * j);
                ++j;
            }
    return boundaries;
}

void VoxelOctreeNode::build(JarVoxelVolume &terrain, BuildContext *ctx)
{
    const int oldLod = LoD;
    LoD = terrain.desired_lod(*this);

    if (LoD < 0)
        return;

    if (!has_flag(FlagGenerated))
    {
        const JarSignedDistanceField::DetailScope detail(edge_length(terrain.get_voxel_size()));
        const float value = terrain.get_sdf_ptr()->distance(_center);
        set_value(value);
        set_flag(FlagGenerated, true);
        // A leaf for good (no surface to refine, or the finest size): a fully
        // realized subtree, not walked again.
        if (!(has_surface(terrain, value) && _size > LoD) && (_size > LoD || _size == min_size()))
        {
            mark_materialized();
            return;
        }
    }
    // Refined where the surface needs this LOD. A leaf at its LOD's size
    // stays walked (not materialized) for when the LOD gets finer; it used to
    // stay ungenerated for that, and every build evaluated the SDF for all of
    // them again (~57k per build on the 16 km planet). Edited leaves keep
    // their (edited) value whole.
    if (is_leaf() && _size > LoD && !has_flag(FlagEdited) && has_surface(terrain, get_value()))
    {
        subdivide(terrain.get_voxel_size());
        for (auto &child : *_children)
            child.set_flag(FlagGenerated, false); // from the SDF, not copied (as edits do)
    }

    // Detail no longer needed (the viewer moved away) is dropped, or the octree
    // would keep every detail it ever generated. Only once this chunk shows the
    // coarser mesh: until then the finer chunks below are still on screen.
    if (LoD > oldLod && !is_leaf())
        set_flag(FlagPrunePending, true);
    if (has_flag(FlagPrunePending) && is_chunk(terrain) && _chunk != nullptr && !_isEnqueued &&
        _chunk->get_lod() == LoD && _chunk->mesh_version == terrain.get_mesh_version())
    {
        set_flag(FlagPrunePending, false);
        prune_finer_than(LoD);
    }
 
    if (!is_chunk(terrain))
        _meshedEmpty = false;
    else if (!is_leaf())
    {
        const uint64_t boundaries = compute_boundaries(terrain);
        const bool knownEmpty = _meshedEmpty && _chunk == nullptr && _meshedEmptyBoundaries == boundaries;
        if (!knownEmpty && (_chunk == nullptr || _chunk->get_boundaries() != boundaries ||
                            _chunk->mesh_version != terrain.get_mesh_version()))
            queue_update(terrain);
    }

    if (!is_leaf() && !(is_chunk(terrain) && (_chunk != nullptr)) && // || is_enqueued()
        (!is_materialized() || is_above_min_chunk(terrain)))
    {
        if (ctx != nullptr && _size == ctx->forkSize)
            for (auto &child : *_children)
                ctx->subtrees.push_back(&child);
        else
            for (auto &child : *_children)
                child.build(terrain, ctx);
    }

    if (ctx != nullptr && _size >= ctx->forkSize)
        ctx->deferredPost.push_back(this);
    else
        build_post(terrain);
}

void VoxelOctreeNode::build_post(JarVoxelVolume &terrain)
{
    if (!is_chunk(terrain))
        delete_chunk();
}

bool VoxelOctreeNode::has_surface(const JarVoxelVolume &terrain, const float value)
{
    //(3*(1/2)^3)^(1/3) = 1.44224957 for d instead of r
    // The extra leaf and a half (at this node's LOD) matters for the last
    // subdivision: the mesher needs the finest leaves up to a cell beyond the
    // surface, so a node twice the leaf size must be kept up to ~2.6 leaves
    // from it, and SDFs that are not exact distances otherwise lose leaves
    // there: holes in the mesh. 1.75 covers the eroded terrain, whose distances
    // can be ~1.5x too large off the surface by cliffs (holes reappear below
    // about 1.4).
    const float voxel = terrain.get_voxel_size();
    return std::abs(value) < (1 << _size) * voxel * 1.44224957f * 1.75f + 1.5f * (1 << std::max<int>(LoD, 0)) * voxel;
}

bool VoxelOctreeNode::modify_children(JarVoxelVolume &terrain, const ModifySettings &settings, EditContext *ctx)
{
    if (is_leaf())
        return true;
    if (ctx == nullptr)
    {
        for (auto &child : *_children)
            child.modify_sdf_in_bounds(terrain, settings);
        return true;
    }
    ctx->shared.push_back(this);
    for (auto &child : *_children)
    {
        if (_size == ctx->forkSize)
            ctx->forks.push_back(&child);
        else
            child.modify_sdf_in_bounds(terrain, settings, ctx);
    }
    return false;
}

void VoxelOctreeNode::finish_modify(JarVoxelVolume &terrain, const ModifySettings &settings)
{
    _meshedEmpty = false; // values changed
    if (is_chunk(terrain))
        queue_update(terrain, true);
    else if (!settings.paintOnly && _chunk != nullptr)
        delete_chunk();
}

void VoxelOctreeNode::modify_sdf_in_bounds(JarVoxelVolume &terrain, const ModifySettings &settings, EditContext *ctx)
{
    auto bounds = get_bounds(terrain.get_voxel_size());
    if (!settings.bounds.intersects(bounds))
        return;
    VoxelStats::add_count(VoxelStats::EditNodes);
    const float sdf_value = settings.sample(_center);
    // Half the node's diagonal, rounded up: nothing in it is nearer than this
    // to the shape, so nothing in it can change.
    if (settings.skipBeyond >= 0.0f && sdf_value > 0.87f * edge_length(terrain.get_voxel_size()) + settings.skipBeyond)
        return;

    LoD = terrain.desired_lod(*this);
    if (!has_flag(FlagGenerated))
    {
        const JarSignedDistanceField::DetailScope detail(edge_length(terrain.get_voxel_size()));
        set_value(terrain.get_sdf_ptr()->distance(_center));
    }

    if (settings.paintOnly)
    {
        // Full strength inside the brush, fading out over one voxel outside it.
        const float inside = -sdf_value / terrain.get_voxel_size();
        if (inside > -1.0f && settings.material >= 0)
        {
            _materials = MaterialWeights::mix(_materials, settings.material,
                                              std::clamp(inside + 1.0f, 0.0f, 1.0f));
            set_flag(FlagGenerated, true);
            mark_edited();
            if (_parent != nullptr)
                _parent->set_dirty(true); // parents average the weights
        }
        if (modify_children(terrain, settings, ctx))
            finish_modify(terrain, settings);
        return;
    }

    float old_value = get_value();
    const float k = settings.smoothness > 0.0f ? settings.smoothness : terrain.get_voxel_size();
    float new_value = SDF::apply_operation(settings.operation, old_value, sdf_value, k);

    // Where the edit changes the field, record the resulting surface normal
    // (exact, from both fields' gradients) for dual contouring.
    if (std::abs(new_value - old_value) > 1e-5f)
    {
        glm::vec3 oldNormal(0.0f);
        if (SDF::uses_first_normal(settings.operation, old_value, sdf_value, k))
            oldNormal = has_normal() ? get_normal() : terrain.get_sdf_ptr()->normal(_center);
        float combined;
        glm::vec3 normal;
        SDF::apply_operation(settings.operation, old_value, oldNormal, sdf_value, settings.gradient(_center), k,
                             combined, normal);
        const float len = glm::length(normal);
        if (len > 1e-6f)
            _normal = Octahedral::encode(normal / len);
    }

    // ensure the node has children if it contains a surface
    if (has_surface(terrain, new_value)) // || has_surface(terrain, sdf_value)
    {
        const bool wasLeaf = is_leaf();
        subdivide(terrain.get_voxel_size());
        // New children copy this node's single value. If it still is the
        // volume's own SDF (never changed by an edit), sample the SDF at each
        // child instead, so edits next to untouched terrain stay accurate.
        if (wasLeaf && !is_leaf() && !has_normal())
            for (auto &child : *_children)
            {
                const JarSignedDistanceField::DetailScope detail(child.edge_length(terrain.get_voxel_size()));
                child.set_value(terrain.get_sdf_ptr()->distance(child._center));
            }
    }
    else if (settings.bounds.encloses(bounds) && !subtree_has_chunk_below())
        prune_children(); // not under chunks still shown or queued: they hold pointers to these

    set_value(new_value);
    set_flag(FlagGenerated, true);
    if (std::abs(new_value - old_value) > 1e-5f)
        mark_edited();
    // Paint the edit's material as a one-hot weight vector. Weights blend at
    // material boundaries via the parent-average and the mesher's edge
    // interpolation; all-zero means "unpainted" (procedural shader coloring).
    if (settings.materials && std::abs(new_value - old_value) > 0.01f)
        _materials = settings.materials->at(settings.to_sdf(_center));
    else if (settings.material >= 0 && std::abs(new_value - old_value) > 0.01f)
    {
        _materials = MaterialWeights::one_hot(settings.material);
    }

    if (is_leaf())
        mark_materialized();
    if (modify_children(terrain, settings, ctx))
        finish_modify(terrain, settings);
}

void VoxelOctreeNode::update_chunk(JarVoxelVolume &terrain, ChunkMeshData *chunkMeshData)
{
    set_enqueued(false);
    finished_meshing_notify_related();
    // An empty result's boundaries were computed by its mesh task
    // (set_empty_boundaries): 26 LOD lookups each, thousands per commit.
    _meshedEmpty = chunkMeshData == nullptr && is_chunk(terrain);
    if (chunkMeshData == nullptr || !is_chunk(terrain))
    {
        delete chunkMeshData; // owned by us until handed to a chunk
        delete_chunk();
        return;
    }

    if (_chunk == nullptr)
    {
        set_chunk(terrain.pool_acquire()); // reuse a pooled chunk, or instantiate one
    }

    _chunk->update_chunk(terrain, this, chunkMeshData);
}

void VoxelOctreeNode::mark_edited()
{
    for (VoxelOctreeNode *n = this; n != nullptr && !n->has_flag(FlagEdited); n = n->_parent)
        n->set_flag(FlagEdited, true);
}

bool VoxelOctreeNode::subtree_has_chunk() const
{
    if (_chunk != nullptr || _isEnqueued)
        return true;
    if (!is_leaf())
        for (const auto &child : *_children)
            if (child.subtree_has_chunk())
                return true;
    return false;
}

// Drops descendants finer than `lod` (keeping nodes of size `lod` as leaves),
// except edited nodes, whose detail exists nowhere else; around them it goes
// on (stopping at the first edited node kept everything under a coarse chunk
// for one small dig: 2.4 million nodes). Pruned nodes are regenerated from the
// SDF if the viewer comes back. Their values are the averages the current
// mesh was built from, so the mesh does not change.
void VoxelOctreeNode::prune_finer_than(int lod)
{
    if (is_leaf())
        return;
    if (_size > lod || has_flag(FlagEdited))
    {
        for (auto &child : *_children)
            child.prune_finer_than(lod);
        return;
    }
    if (subtree_has_chunk())
        return;
    update_if_dirty();
    prune_children();
    set_flag(FlagGenerated, false);
    // Builds skip materialized subtrees; this one must be walked again to
    // regenerate. Clear the marks up to the root.
    for (VoxelOctreeNode *n = this; n != nullptr && n->_isMaterialized != 0; n = n->_parent)
        n->_isMaterialized = 0;
}

void VoxelOctreeNode::queue_update(JarVoxelVolume &terrain, bool urgent)
{
    set_enqueued(true);
    terrain.enqueue_chunk_update(*this, urgent);
}

void VoxelOctreeNode::get_chunks_touching(const JarVoxelVolume &terrain, const Bounds &bounds,
                                          std::vector<VoxelOctreeNode *> &result)
{
    if (!get_bounds(terrain.get_voxel_size()).intersects(bounds))
        return;
    if (is_chunk(terrain))
    {
        result.push_back(this);
        return;
    }
    if (is_leaf() || _size <= terrain.get_min_chunk_size())
        return; // nothing below is a chunk
    for (auto &child : *_children)
        child.get_chunks_touching(terrain, bounds, result);
}

VoxelOctreeNode *VoxelOctreeNode::node_at(float voxelSize, const glm::vec3 &point)
{
    if (!get_bounds(voxelSize).contains_point(point))
        return nullptr;
    VoxelOctreeNode *node = this;
    while (!node->is_leaf())
    {
        const glm::vec3 &c = node->_center;
        node = &(*node->_children)[(point.x >= c.x ? 1 : 0) + (point.y >= c.y ? 2 : 0) + (point.z >= c.z ? 4 : 0)];
    }
    return node;
}

bool VoxelOctreeNode::edited_around(float voxelSize, const Bounds &bounds)
{
    VoxelOctreeNode *node = this;
    while (!node->is_leaf() && node->has_flag(FlagEdited))
    {
        VoxelOctreeNode *inner = nullptr;
        for (auto &child : *node->_children)
            if (child.get_bounds(voxelSize).encloses(bounds))
                inner = &child;
        if (inner == nullptr)
            break;
        node = inner;
    }
    return node->has_flag(FlagEdited);
}

void VoxelOctreeNode::get_shown_chunks_replaced(std::vector<VoxelOctreeNode *> &result)
{
    for (VoxelOctreeNode *ancestor = _parent; ancestor != nullptr; ancestor = ancestor->_parent)
        if (ancestor->_chunk != nullptr)
            result.push_back(ancestor);
    get_shown_chunks_replaced_below(result);
}

void VoxelOctreeNode::get_shown_chunks_replaced_below(std::vector<VoxelOctreeNode *> &result)
{
    if (is_leaf())
        return;
    for (auto &child : *_children)
    {
        if (child._chunksBelow.load(std::memory_order_relaxed) == 0)
            continue;
        if (child._chunk != nullptr)
            result.push_back(&child);
        child.get_shown_chunks_replaced_below(result);
    }
}

void VoxelOctreeNode::set_enqueued(bool value)
{
    if (_isEnqueued == value)
        return;
    _isEnqueued = value;
    for (VoxelOctreeNode *n = this; n != nullptr; n = n->_parent)
        n->_enqueuedBelow.fetch_add(value ? 1 : -1, std::memory_order_relaxed);
}

void VoxelOctreeNode::set_chunk(JarVoxelChunk *chunk)
{
    if ((_chunk != nullptr) != (chunk != nullptr))
        for (VoxelOctreeNode *n = this; n != nullptr; n = n->_parent)
            n->_chunksBelow.fetch_add(chunk != nullptr ? 1 : -1, std::memory_order_relaxed);
    _chunk = chunk;
}

void VoxelOctreeNode::delete_chunk()
{
    // Keep it on screen until whatever replaces it has been meshed.
    if (_chunk == nullptr || is_any_descendant_enqueued() || is_any_ancestor_enqueued())
        return;

    if (_chunk != nullptr)
    {
        // Return the chunk to its owning terrain's pool for reuse instead of
        // freeing it (thread-safe; called from the build worker). The chunk's
        // parent is always the terrain that acquired it.
        if (JarVoxelVolume *t = Object::cast_to<JarVoxelVolume>(_chunk->get_parent()))
            t->pool_release(_chunk);
        else
            _chunk->queue_free();
    }
    set_chunk(nullptr);
}

void VoxelOctreeNode::get_voxel_leaves_in_bounds(const JarVoxelVolume &terrain, const Bounds &bounds,
                                                 std::vector<VoxelOctreeNode *> &result)
{
    if (!get_bounds(terrain.get_voxel_size()).intersects(bounds))
        return;

    // LoD = terrain.get_lod()->desired_lod(*this);

    if (_size == LoD || (is_leaf() && _size >= LoD))
    {
        result.push_back(this);
        return;
    }

    if (is_chunk(terrain))
        for (auto &child : *_children) // use all the same LoD from here on out
            child.get_voxel_leaves_in_bounds(terrain, bounds, LoD, result);
    else
        for (auto &child : *_children)
            child.get_voxel_leaves_in_bounds(terrain, bounds, result);
}

void VoxelOctreeNode::get_voxel_leaves_in_bounds(const JarVoxelVolume &terrain, const Bounds &bounds, const int LOD,
                                                 std::vector<VoxelOctreeNode *> &result)
{
    if (!get_bounds(terrain.get_voxel_size()).intersects(bounds) || (is_leaf() && _size > LOD))
        return;

    if (_size == LOD)
    {
        result.push_back(this);
        return;
    }

    for (auto &child : *_children)
        child.get_voxel_leaves_in_bounds(terrain, bounds, LOD, result);
}

void VoxelOctreeNode::get_voxel_leaves_in_bounds_excluding_bounds(const JarVoxelVolume &terrain,
                                                                  const Bounds &acceptance_bounds,
                                                                  const Bounds &rejection_bounds, const int LOD,
                                                                  std::vector<VoxelOctreeNode *> &result)
{
    auto bounds = get_bounds(terrain.get_voxel_size());
    if (!acceptance_bounds.intersects(bounds) || (is_leaf() && _size > LOD))
        return;

    if (_size == LOD)
    {
        if (!rejection_bounds.intersects(bounds))
            result.push_back(this);
        return;
    }

    for (auto &child : *_children)
        child.get_voxel_leaves_in_bounds_excluding_bounds(terrain, acceptance_bounds, rejection_bounds, LOD, result);
}

void VoxelOctreeNode::sample_into(const JarVoxelVolume &terrain, VoxelBlock &block)
{
    const float half = 0.5f * block.spacing;
    const Bounds bounds = get_bounds(terrain.get_voxel_size());
    const Bounds reach(block.origin - glm::vec3(half),
                       block.origin + glm::vec3(block.size) * block.spacing - glm::vec3(half));
    if (!bounds.intersects(reach))
        return;
    if (!is_leaf())
    {
        for (auto &child : *_children)
            child.sample_into(terrain, block);
        return;
    }
    // A leaf: the cells whose centers it holds take its sample.
    const glm::vec3 lo = (bounds.min - block.origin) / block.spacing;
    const glm::vec3 hi = (bounds.max - block.origin) / block.spacing;
    const glm::ivec3 first = glm::max(glm::ivec3(glm::ceil(lo - 1e-3f)), glm::ivec3(0));
    const glm::ivec3 last = glm::min(glm::ivec3(glm::floor(hi - 1e-3f)), block.size - 1);
    const float value = get_value();
    for (int z = first.z; z <= last.z; ++z)
        for (int y = first.y; y <= last.y; ++y)
            for (int x = first.x; x <= last.x; ++x)
            {
                const int64_t i = block.index(glm::ivec3(x, y, z));
                block.values[i] = value;
                block.materials[i] = _materials;
                block.normals[i] = _normal;
            }
}
