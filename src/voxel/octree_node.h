#ifndef OCTREE_NODE_H
#define OCTREE_NODE_H

#include "bounds.h"
#include <array>
#include <glm/glm.hpp>
#include <memory>

template <typename TNode>
class OctreeNode {
public:
    std::unique_ptr<std::array<std::unique_ptr<TNode>, 8>> _children;
    TNode* _parent = nullptr;
    const glm::vec3 _center = {0, 0, 0};
    const int _size = 0;

    OctreeNode(TNode* parent, const glm::vec3& center, int size)
        : _parent(parent), _center(center), _size(size) {}

    ~OctreeNode() {
        prune_children();
    }

    inline bool is_leaf() const {
        return !_children; // Check if _children is null
    }

    inline float edge_length(float scale) const {
        return (1 << _size) * scale;
    }

    inline void prune_children() {
        _children.reset(); // Automatically deletes children
    }

    inline Bounds get_bounds(float scale) const {
        auto halfEdge = glm::vec3(edge_length(scale) * 0.5f);
        return Bounds(_center - halfEdge, _center + halfEdge);
    }

    void subdivide(float scale) {
        if (_size <= min_size() || !is_leaf())
            return;

        float childOffset = edge_length(scale) * 0.25f;
        int childSize = _size - 1;
        static const glm::vec3 ChildPositions[] = {
            {-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
            {-1, -1, 1},  {1, -1, 1},  {-1, 1, 1},  {1, 1, 1}
        };

        // Allocate array and create children
        _children = std::make_unique<std::array<std::unique_ptr<TNode>, 8>>();
        for (int i = 0; i < 8; ++i) {
            (*_children)[i] = create_child_node(
                _center + ChildPositions[i] * childOffset, childSize
            );
        }
    }

    int get_count() const {
        int count = 1;
        if (!is_leaf()) {
            for (const auto& child : *_children) {
                count += child->get_count();
            }
        }
        return count;
    }

protected:
    virtual int min_size() const { return 0; }
    virtual std::unique_ptr<TNode> create_child_node(const glm::vec3& center, int size) = 0;
};

#endif // OCTREE_NODE_H