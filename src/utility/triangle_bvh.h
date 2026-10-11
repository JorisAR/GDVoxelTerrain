#ifndef JAR_TRIANGLE_BVH_H
#define JAR_TRIANGLE_BVH_H

#include "bounds.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

// Bounding volume hierarchy over a triangle soup, for the two queries a mesh
// SDF bake needs: distance to the closest triangle, and every crossing of an
// axis-aligned ray (to decide inside/outside by parity). Read-only after
// build(), so queries are safe from many threads at once.
class TriangleBvh
{
  public:
    // `corners` holds 3 vertices per triangle.
    void build(const std::vector<glm::vec3> &corners);
    bool empty() const { return _tris.empty(); }
    Bounds bounds() const { return _nodes.empty() ? Bounds() : _nodes[0].bounds; }

    float closest_distance(const glm::vec3 &p) const;
    // Appends the ray parameter t of every triangle crossing of the ray
    // origin + t * e_axis (t > 0), unsorted.
    void axis_ray_hits(const glm::vec3 &origin, int axis, std::vector<float> &hits) const;

  private:
    struct Tri
    {
        glm::vec3 a, b, c;
    };
    struct Node
    {
        Bounds bounds;
        uint32_t first = 0; // leaf: first index into _order; inner: left child (right = left + 1)
        uint32_t count = 0; // > 0 for leaves
    };

    std::vector<Tri> _tris;
    std::vector<uint32_t> _order; // triangle indices, grouped by leaf
    std::vector<Node> _nodes;

    void build_node(uint32_t node, uint32_t first, uint32_t count, const std::vector<glm::vec3> &centroids);
    static float point_triangle_distance_sq(const glm::vec3 &p, const Tri &t);
    static float aabb_distance_sq(const Bounds &b, const glm::vec3 &p);
};

#endif // JAR_TRIANGLE_BVH_H
