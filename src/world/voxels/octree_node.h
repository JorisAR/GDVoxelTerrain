#ifndef OCTREE_NODE_H
#define OCTREE_NODE_H

#include "bounds.h"
#include <array>
#include <optional>
#include <glm/glm.hpp>
#include <memory>

namespace godot
{

template <typename TNode> class OctreeNode
{
  public:
    OctreeNode(TNode *parent, const glm::vec3 &center, uint32_t size) : _parent(parent), _center(center), _sizeLog2(size)
    {
    }

    ~OctreeNode()
    {
        prune_children();
    }

    inline bool is_leaf() const
    {
        return !_children.has_value();
    }

    inline float edge_length(float scale) const
    {
        return (1 << _sizeLog2) * scale;
    }

    inline void prune_children()
    {
        _children.reset(); // Automatically deletes children
    }

    inline Bounds get_bounds(float scale) const
    {
        auto halfEdge = glm::vec3(edge_length(scale) * 0.5f);
        return Bounds(_center - halfEdge, _center + halfEdge);
    }

    inline glm::vec3 get_center() const
    {
        return _center;
    }

    void subdivide(float scale)
    {
        if (_sizeLog2 <= min_size() || !is_leaf())
            return;

        float childOffset = edge_length(scale) * 0.25f;
        int childSize = _sizeLog2 - 1;
        static const glm::vec3 ChildPositions[] = {{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
                                                   {-1, -1, 1},  {1, -1, 1},  {-1, 1, 1},  {1, 1, 1}};

        // Allocate array and create children
        _children.emplace(); // Initializes the optional with a default-constructed array
        for (int i = 0; i < 8; ++i)
        {
            _children->at(i) = create_child_node(_center + ChildPositions[i] * childOffset, childSize);
        }
    }

    int get_count() const
    {
        int count = 1;
        if (!is_leaf())
        {
            for (const auto &child : *_children)
            {
                count += child->get_count();
            }
        }
        return count;
    }

    inline const std::optional<std::array<std::unique_ptr<TNode>, 8>> &get_children() const
    {
        return _children;
    }

    inline uint32_t get_size_log2() const
    {
        return _sizeLog2;
    }

  protected:
    std::optional<std::array<std::unique_ptr<TNode>, 8>> _children;
    TNode *_parent = nullptr;
    const glm::vec3 _center = {0, 0, 0};
    const uint32_t _sizeLog2 = 0;

    virtual uint32_t min_size() const
    {
        return 0;
    }
    virtual std::unique_ptr<TNode> create_child_node(const glm::vec3 &center, uint32_t size) = 0;
};
} // namespace godot

#endif // OCTREE_NODE_H