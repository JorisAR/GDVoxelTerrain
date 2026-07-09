#include "voxel_octree_node.h"
#include "voxel_terrain.h"
#include <algorithm>
#include <cmath>
#include <execution>

VoxelOctreeNode::VoxelOctreeNode(int size) : VoxelOctreeNode(nullptr, glm::vec3(0.0f), size)
{
}

VoxelOctreeNode::VoxelOctreeNode(VoxelOctreeNode *parent, const glm::vec3 center, int size)
    : OctreeNode(parent, center, size), _isMaterialized(0b00000000)
{
    if (_parent != nullptr)
    {
        LoD = _parent->LoD;
        _value = _parent->get_value();
        _isSet = _parent->_isSet;
        NodeColor = _parent->NodeColor;
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

// todo, make threadsafe. It's currently maybe fine (due to the way the scheduler is set up), but better safe than
// sorry.
float VoxelOctreeNode::get_value()
{
    if (!is_dirty())
        return _value;
    if (!is_leaf())
    {
        _value = 0;
        NodeColor = glm::vec4(0, 0, 0, 0);
        for (auto &child : (*_children))
        {
            _value += child->get_value();
            NodeColor += child->NodeColor;
        }
        _value *= 0.125f;
        NodeColor *= 0.125f;
    }
    _isDirty = false;
    return _value;
}

int VoxelOctreeNode::get_lod() const
{
    return LoD;
}

glm::vec4 VoxelOctreeNode::get_color() const
{
    return NodeColor;
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
            _isMaterialized |= _children->at(i)->is_materialized() ? 1 : 0 << i;
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

bool VoxelOctreeNode::is_chunk(const JarVoxelTerrain &terrain) const
{
    return _size == (LoD + terrain.get_min_chunk_size());
}

bool VoxelOctreeNode::is_above_chunk(const JarVoxelTerrain &terrain) const
{
    return _size > (LoD + terrain.get_min_chunk_size());
}

bool VoxelOctreeNode::is_above_min_chunk(const JarVoxelTerrain &terrain) const
{
    return _size > (terrain.get_min_chunk_size());
}

bool VoxelOctreeNode::is_one_above_chunk(const JarVoxelTerrain &terrain) const
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

bool VoxelOctreeNode::should_delete_chunk(const JarVoxelTerrain &terrain) const
{
    return false;
}

uint16_t VoxelOctreeNode::compute_boundaries(const JarVoxelTerrain &terrain) const
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

void VoxelOctreeNode::build(JarVoxelTerrain &terrain)
{
    LoD = terrain.desired_lod(*this);

    if (LoD < 0)
        return;

    if (!_isSet)
    {
        float value = terrain.get_sdf()->distance(_center);
        set_value(value);
        if (has_surface(terrain, value) && (_size > LoD))
        {
            subdivide(terrain.get_octree_scale());
            _isSet = true;
        }
        // if we don't subdivide further, we mark it as a fully realized subtree
        if (is_leaf() && (_size > LoD || _size == min_size()))
        { //
            _isSet = true;
            mark_materialized();
            return;
        }
    }
 
    if (is_chunk(terrain) && !is_leaf() &&
        (_chunk == nullptr || (_chunk->get_boundaries() != compute_boundaries(terrain))))
        queue_update(terrain);

    if (!is_leaf() && !(is_chunk(terrain) && (_chunk != nullptr)) && // || is_enqueued()
        (!is_materialized() || is_above_min_chunk(terrain)))
        for (auto &child : *_children)
            child->build(terrain);

    if (!is_chunk(terrain))
        delete_chunk();
}

bool VoxelOctreeNode::has_surface(const JarVoxelTerrain &terrain, const float value)
{
    //(3*(1/2)^3)^(1/3) = 1.44224957 for d instead of r
    return std::abs(value) < (1 << _size) * terrain.get_octree_scale() * 1.44224957f * 1.75f; 
}

void VoxelOctreeNode::modify_sdf_in_bounds(JarVoxelTerrain &terrain, const ModifySettings &settings)
{
    if (settings.sdf.is_null())
    {
        UtilityFunctions::print("sdf invalid");
        return;
    }

    auto bounds = get_bounds(terrain.get_octree_scale());
    if (!settings.bounds.intersects(bounds))
        return;

    LoD = terrain.desired_lod(*this);
    if (!_isSet)
        set_value(terrain.get_sdf()->distance(_center));

    float old_value = get_value();
    float sdf_value = settings.sdf->distance(_center - settings.position);
    float new_value = SDF::apply_operation(settings.operation, old_value, sdf_value, terrain.get_octree_scale());

    // ensure the node has children if it contains a surface
    if (has_surface(terrain, new_value)) // || has_surface(terrain, sdf_value)
        subdivide(terrain.get_octree_scale());
    else
        if(settings.bounds.encloses(bounds))
            prune_children();

    set_value(new_value);
    _isSet = true;
    // Paint the edit's material as a one-hot weight vector. Weights blend at
    // material boundaries via the parent-average and the mesher's edge
    // interpolation; all-zero means "unpainted" (procedural shader coloring).
    if (settings.material >= 0 && std::abs(new_value - old_value) > 0.01f)
    {
        glm::vec4 weights(0.0f);
        weights[glm::clamp(settings.material, 0, 3)] = 1.0f;
        NodeColor = weights;
    }

    if (is_leaf())
        mark_materialized();
    else // recurse down the tree
        for (auto &child : *_children)
            child->modify_sdf_in_bounds(terrain, settings);

    if (is_chunk(terrain))
        queue_update(terrain);
    else if (_chunk != nullptr)
        delete_chunk();
}

void VoxelOctreeNode::update_chunk(JarVoxelTerrain &terrain, ChunkMeshData *chunkMeshData)
{
    _isEnqueued = false;
    finished_meshing_notify_parent_and_children();
    if (chunkMeshData == nullptr || !is_chunk(terrain))
    {
        delete_chunk();
        return;
    }

    if (_chunk == nullptr)
    {
        _chunk = terrain.pool_acquire(); // reuse a pooled chunk, or instantiate one
    }

    _chunk->update_chunk(terrain, this, chunkMeshData);
}

void VoxelOctreeNode::queue_update(JarVoxelTerrain &terrain)
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
        if (JarVoxelTerrain *t = Object::cast_to<JarVoxelTerrain>(_chunk->get_parent()))
            t->pool_release(_chunk);
        else
            _chunk->queue_free();
    }
    _chunk = nullptr;
}

void VoxelOctreeNode::get_voxel_leaves_in_bounds(const JarVoxelTerrain &terrain, const Bounds &bounds,
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

void VoxelOctreeNode::get_voxel_leaves_in_bounds(const JarVoxelTerrain &terrain, const Bounds &bounds, const int LOD,
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

void VoxelOctreeNode::get_voxel_leaves_in_bounds_excluding_bounds(const JarVoxelTerrain &terrain,
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
