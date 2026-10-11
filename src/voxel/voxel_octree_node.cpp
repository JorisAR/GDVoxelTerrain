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
        _isGenerated = _parent->_isGenerated;
        _nodeColor = _parent->_nodeColor;
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
        _value = 0.0f;
        _nodeColor = glm::vec4(0.0f);

        for (auto &child : (*_children))
        {
            _value     += child->get_value();
            _nodeColor += child->get_color();
        }

        // Average over 8 children
        _value     *= 0.125f;
        _nodeColor *= 0.125f;
    }

    _isDirty = false;
}

float VoxelOctreeNode::get_value()
{
    update_if_dirty();
    return _value;
}

glm::vec4 VoxelOctreeNode::get_color()
{
    update_if_dirty();
    return _nodeColor;
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
            _isMaterialized |= (_children->at(i)->is_materialized() ? 1 : 0) << i;
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
        child->populateUniqueLoDValues(lodValues);
    }
}

bool VoxelOctreeNode::is_enqueued() const
{
    return _isEnqueued;
}

// Called when a queued mesh task is dropped (node stopped being a chunk). A node
// left flagged as enqueued blocks delete_chunk() on its parent and children
// forever, leaving stale chunks (and their details) in the scene.
void VoxelOctreeNode::cancel_enqueue()
{
    _isEnqueued = false;
}

void VoxelOctreeNode::finished_meshing_notify_parent_and_children() const
{
    if (_parent != nullptr)
    {
        _parent->delete_chunk();
    }
    if (!is_leaf())
    {
        for (auto &child : *_children)
        {
            child->delete_chunk();
        }
    }
}

bool VoxelOctreeNode::is_parent_enqueued() const
{
    return _parent == nullptr ? false : _parent->is_enqueued();
}

bool VoxelOctreeNode::is_any_children_enqueued() const
{
    if (is_leaf())
        return false;
    for (const auto &child : *_children)
    {
        if (child->is_enqueued())
            return true;
    }
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
    const float el = edge_length(terrain.get_octree_scale());
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
    LoD = terrain.desired_lod(*this);

    if (LoD < 0)
        return;

    if (!_isGenerated)
    {
        float value = terrain.get_sdf_ptr()->distance(_center);
        set_value(value);
        if (has_surface(terrain, value) && (_size > LoD))
        {
            subdivide(terrain.get_octree_scale());
            _isGenerated = true;
        }
        // if we don't subdivide further, we mark it as a fully realized subtree
        if (is_leaf() && (_size > LoD || _size == min_size()))
        { //
            _isGenerated = true;
            mark_materialized();
            return;
        }
    }
 
    if (!is_chunk(terrain))
        _meshedEmpty = false;
    else if (!is_leaf())
    {
        const uint16_t boundaries = compute_boundaries(terrain);
        const bool knownEmpty = _meshedEmpty && _chunk == nullptr && _meshedEmptyBoundaries == boundaries;
        if (!knownEmpty && (_chunk == nullptr || _chunk->get_boundaries() != boundaries))
            queue_update(terrain);
    }

    if (!is_leaf() && !(is_chunk(terrain) && (_chunk != nullptr)) && // || is_enqueued()
        (!is_materialized() || is_above_min_chunk(terrain)))
    {
        if (ctx != nullptr && _size == ctx->forkSize)
            for (auto &child : *_children)
                ctx->subtrees.push_back(child.get());
        else
            for (auto &child : *_children)
                child->build(terrain, ctx);
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
    return std::abs(value) < (1 << _size) * terrain.get_octree_scale() * 1.44224957f * 1.75f; 
}

void VoxelOctreeNode::modify_sdf_in_bounds(JarVoxelVolume &terrain, const ModifySettings &settings)
{
    auto bounds = get_bounds(terrain.get_octree_scale());
    if (!settings.bounds.intersects(bounds))
        return;

    LoD = terrain.desired_lod(*this);
    if (!_isGenerated)
        set_value(terrain.get_sdf_ptr()->distance(_center));

    float old_value = get_value();
    float sdf_value = settings.sample(_center);
    const float k = settings.smoothness > 0.0f ? settings.smoothness : terrain.get_octree_scale();
    float new_value = SDF::apply_operation(settings.operation, old_value, sdf_value, k);

    // ensure the node has children if it contains a surface
    if (has_surface(terrain, new_value)) // || has_surface(terrain, sdf_value)
        subdivide(terrain.get_octree_scale());
    else
        if(settings.bounds.encloses(bounds))
            prune_children();

    set_value(new_value);
    _isGenerated = true;
    // Paint the edit's material as a one-hot weight vector. Weights blend at
    // material boundaries via the parent-average and the mesher's edge
    // interpolation; all-zero means "unpainted" (procedural shader coloring).
    if (settings.material >= 0 && std::abs(new_value - old_value) > 0.01f)
    {
        glm::vec4 weights(0.0f);
        weights[glm::clamp(settings.material, 0, 3)] = 1.0f;
        _nodeColor = weights;
    }

    if (is_leaf())
        mark_materialized();
    else // recurse down the tree
        for (auto &child : *_children)
            child->modify_sdf_in_bounds(terrain, settings);

    _meshedEmpty = false; // values changed
    if (is_chunk(terrain))
        queue_update(terrain);
    else if (_chunk != nullptr)
        delete_chunk();
}

void VoxelOctreeNode::update_chunk(JarVoxelVolume &terrain, ChunkMeshData *chunkMeshData)
{
    _isEnqueued = false;
    finished_meshing_notify_parent_and_children();
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

void VoxelOctreeNode::queue_update(JarVoxelVolume &terrain)
{
    if (_isEnqueued)
        return;
    _isEnqueued = true;
    terrain.enqueue_chunk_update(*this);
}

void VoxelOctreeNode::delete_chunk()
{
    if (is_any_children_enqueued() || is_parent_enqueued())
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
    if (!get_bounds(terrain.get_octree_scale()).intersects(bounds))
        return;

    // LoD = terrain.get_lod()->desired_lod(*this);

    if (_size == LoD || (is_leaf() && _size >= LoD))
    {
        result.push_back(this);
        return;
    }

    if (is_chunk(terrain))
        for (auto &child : *_children) // use all the same LoD from here on out
            child->get_voxel_leaves_in_bounds(terrain, bounds, LoD, result);
    else
        for (auto &child : *_children)
            child->get_voxel_leaves_in_bounds(terrain, bounds, result);
}

void VoxelOctreeNode::get_voxel_leaves_in_bounds(const JarVoxelVolume &terrain, const Bounds &bounds, const int LOD,
                                                 std::vector<VoxelOctreeNode *> &result)
{
    if (!get_bounds(terrain.get_octree_scale()).intersects(bounds) || (is_leaf() && _size > LOD))
        return;

    if (_size == LOD)
    {
        result.push_back(this);
        return;
    }

    for (auto &child : *_children)
        child->get_voxel_leaves_in_bounds(terrain, bounds, LOD, result);
}

void VoxelOctreeNode::get_voxel_leaves_in_bounds_excluding_bounds(const JarVoxelVolume &terrain,
                                                                  const Bounds &acceptance_bounds,
                                                                  const Bounds &rejection_bounds, const int LOD,
                                                                  std::vector<VoxelOctreeNode *> &result)
{
    auto bounds = get_bounds(terrain.get_octree_scale());
    if (!acceptance_bounds.intersects(bounds) || (is_leaf() && _size > LOD))
        return;

    if (_size == LOD)
    {
        if (!rejection_bounds.intersects(bounds))
            result.push_back(this);
        return;
    }

    for (auto &child : *_children)
        child->get_voxel_leaves_in_bounds_excluding_bounds(terrain, acceptance_bounds, rejection_bounds, LOD, result);
}

std::unique_ptr<VoxelOctreeNode> VoxelOctreeNode::create_child_node(const glm::vec3 &center, int size)
{
    return std::make_unique<VoxelOctreeNode>(this, center, size);
}
