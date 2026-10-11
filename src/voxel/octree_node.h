#ifndef OCTREE_NODE_H
#define OCTREE_NODE_H

#include "bounds.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <new>

// The 8 children of a node, allocated together in one block: one allocation
// instead of eight (plus a pointer array), which matters with millions of nodes.
// Iterating yields the nodes themselves.
template <typename TNode>
class OctreeChildren
{
    alignas(TNode) unsigned char _storage[8 * sizeof(TNode)];

  public:
    template <typename Make>
    explicit OctreeChildren(Make &&make)
    {
        for (int i = 0; i < 8; ++i)
            make(static_cast<void *>(_storage + i * sizeof(TNode)), i);
    }
    ~OctreeChildren()
    {
        for (int i = 7; i >= 0; --i)
            (*this)[i].~TNode();
    }
    OctreeChildren(const OctreeChildren &) = delete;
    OctreeChildren &operator=(const OctreeChildren &) = delete;

    TNode &operator[](int i) { return *std::launder(reinterpret_cast<TNode *>(_storage + i * sizeof(TNode))); }
    const TNode &operator[](int i) const
    {
        return *std::launder(reinterpret_cast<const TNode *>(_storage + i * sizeof(TNode)));
    }
    TNode *begin() { return &(*this)[0]; }
    TNode *end() { return begin() + 8; }
    const TNode *begin() const { return &(*this)[0]; }
    const TNode *end() const { return begin() + 8; }
};

// Octree node base (CRTP: no virtual functions, so no vtable pointer per node).
// TNode must be constructible as TNode(TNode *parent, glm::vec3 center, int size)
// and may hide min_size().
template <typename TNode>
class OctreeNode {
public:
    std::unique_ptr<OctreeChildren<TNode>> _children;
    TNode* _parent = nullptr;
    const glm::vec3 _center = {0, 0, 0};
    const int8_t _size = 0;

    OctreeNode(TNode* parent, const glm::vec3& center, int size)
        : _parent(parent), _center(center), _size(static_cast<int8_t>(size)) {}

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
        TNode *self = static_cast<TNode *>(this);
        if (_size <= self->min_size() || !is_leaf())
            return;

        float childOffset = edge_length(scale) * 0.25f;
        int childSize = _size - 1;
        static const glm::vec3 ChildPositions[] = {
            {-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
            {-1, -1, 1},  {1, -1, 1},  {-1, 1, 1},  {1, 1, 1}
        };

        const glm::vec3 center = _center;
        _children = std::make_unique<OctreeChildren<TNode>>([&](void *memory, int i) {
            new (memory) TNode(self, center + ChildPositions[i] * childOffset, childSize);
        });
    }

    int get_count() const {
        int count = 1;
        if (!is_leaf()) {
            for (const auto& child : *_children) {
                count += child.get_count();
            }
        }
        return count;
    }

    int min_size() const { return 0; }
};

#endif // OCTREE_NODE_H
