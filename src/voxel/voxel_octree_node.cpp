#include "voxel_octree_node.h"
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
    _isEnqueued = false;
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
        if (child._isEnqueued || child.is_any_descendant_enqueued())
            return true;
    return false;
}

bool VoxelOctreeNode::should_delete_chunk(const JarVoxelVolume &terrain) const
{
    return false;
}

uint16_t VoxelOctreeNode::compute_boundaries(const JarVoxelVolume &terrain) const
{
    static const std::vector<glm::vec3> offsets = {glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0),
        glm::vec3(0, 1, 0), glm::vec3(0, -1, 0),
        glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)};

    uint16_t boundaries = 0;
    const float el = edge_length(terrain.get_voxel_size());
    for (size_t i = 0; i < offsets.size(); ++i)
    {
        int l = terrain.lod_at(_center + el * offsets[i]);
        boundaries |= (LoD < l ? 1 : 0) << i;       // high to low
        boundaries |= (LoD > l ? 1 : 0) << (i + 8); // low to high
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
        float value = terrain.get_sdf_ptr()->distance(_center);
        set_value(value);
        if (has_surface(terrain, value) && (_size > LoD))
        {
            subdivide(terrain.get_voxel_size());
            set_flag(FlagGenerated, true);
        }
        // if we don't subdivide further, we mark it as a fully realized subtree
        if (is_leaf() && (_size > LoD || _size == min_size()))
        { //
            set_flag(FlagGenerated, true);
            mark_materialized();
            return;
        }
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
        const uint16_t boundaries = compute_boundaries(terrain);
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

void VoxelOctreeNode::modify_sdf_in_bounds(JarVoxelVolume &terrain, const ModifySettings &settings)
{
    auto bounds = get_bounds(terrain.get_voxel_size());
    if (!settings.bounds.intersects(bounds))
        return;

    LoD = terrain.desired_lod(*this);
    if (!has_flag(FlagGenerated))
        set_value(terrain.get_sdf_ptr()->distance(_center));

    if (settings.paintOnly)
    {
        // Full strength inside the brush, fading out over one voxel outside it.
        const float inside = -settings.sample(_center) / terrain.get_voxel_size();
        if (inside > -1.0f && settings.material >= 0)
        {
            _materials = MaterialWeights::mix(_materials, settings.material,
                                              std::clamp(inside + 1.0f, 0.0f, 1.0f));
            set_flag(FlagGenerated, true);
            mark_edited();
            if (_parent != nullptr)
                _parent->set_dirty(true); // parents average the weights
        }
        if (!is_leaf())
            for (auto &child : *_children)
                child.modify_sdf_in_bounds(terrain, settings);
        _meshedEmpty = false;
        if (is_chunk(terrain))
            queue_update(terrain);
        return;
    }

    float old_value = get_value();
    float sdf_value = settings.sample(_center);
    const float k = settings.smoothness > 0.0f ? settings.smoothness : terrain.get_voxel_size();
    float new_value = SDF::apply_operation(settings.operation, old_value, sdf_value, k);

    // Where the edit changes the field, record the resulting surface normal
    // (exact, from both fields' gradients) for dual contouring.
    if (std::abs(new_value - old_value) > 1e-5f)
    {
        const glm::vec3 oldNormal = has_normal() ? get_normal() : terrain.get_sdf_ptr()->normal(_center);
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
                child.set_value(terrain.get_sdf_ptr()->distance(child._center));
    }
    else
        if(settings.bounds.encloses(bounds))
            prune_children();

    set_value(new_value);
    set_flag(FlagGenerated, true);
    if (std::abs(new_value - old_value) > 1e-5f)
        mark_edited();
    // Paint the edit's material as a one-hot weight vector. Weights blend at
    // material boundaries via the parent-average and the mesher's edge
    // interpolation; all-zero means "unpainted" (procedural shader coloring).
    if (settings.material >= 0 && std::abs(new_value - old_value) > 0.01f)
    {
        _materials = MaterialWeights::one_hot(settings.material);
    }

    if (is_leaf())
        mark_materialized();
    else // recurse down the tree
        for (auto &child : *_children)
            child.modify_sdf_in_bounds(terrain, settings);

    _meshedEmpty = false; // values changed
    if (is_chunk(terrain))
        queue_update(terrain);
    else if (_chunk != nullptr)
        delete_chunk();
}

void VoxelOctreeNode::update_chunk(JarVoxelVolume &terrain, ChunkMeshData *chunkMeshData)
{
    _isEnqueued = false;
    finished_meshing_notify_related();
    _meshedEmpty = chunkMeshData == nullptr && is_chunk(terrain);
    if (_meshedEmpty)
        _meshedEmptyBoundaries = compute_boundaries(terrain);
    if (chunkMeshData == nullptr || !is_chunk(terrain))
    {
        delete chunkMeshData; // owned by us until handed to a chunk
        delete_chunk();
        return;
    }

    if (_chunk == nullptr)
    {
        _chunk = terrain.pool_acquire(); // reuse a pooled chunk, or instantiate one
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
// except in edited subtrees, whose detail exists nowhere else. Pruned nodes are
// regenerated from the SDF if the viewer comes back. Their values are the
// averages the current mesh was built from, so the mesh does not change.
void VoxelOctreeNode::prune_finer_than(int lod)
{
    if (is_leaf() || has_flag(FlagEdited))
        return;
    if (_size > lod)
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

void VoxelOctreeNode::queue_update(JarVoxelVolume &terrain)
{
    if (_isEnqueued)
        return;
    _isEnqueued = true;
    terrain.enqueue_chunk_update(*this);
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
    _chunk = nullptr;
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


