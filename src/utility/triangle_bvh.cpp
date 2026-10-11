#include "triangle_bvh.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr uint32_t LeafSize = 4;
}

void TriangleBvh::build(const std::vector<glm::vec3> &corners)
{
    _tris.clear();
    _order.clear();
    _nodes.clear();
    const size_t n = corners.size() / 3;
    if (n == 0)
        return;

    _tris.resize(n);
    std::vector<glm::vec3> centroids(n);
    for (size_t i = 0; i < n; ++i)
    {
        _tris[i] = {corners[3 * i], corners[3 * i + 1], corners[3 * i + 2]};
        centroids[i] = (_tris[i].a + _tris[i].b + _tris[i].c) / 3.0f;
    }
    _order.resize(n);
    for (uint32_t i = 0; i < n; ++i)
        _order[i] = i;

    _nodes.reserve(2 * n / LeafSize + 1);
    _nodes.emplace_back();
    build_node(0, 0, uint32_t(n), centroids);
}

void TriangleBvh::build_node(uint32_t node, uint32_t first, uint32_t count, const std::vector<glm::vec3> &centroids)
{
    Bounds bounds, centroidBounds;
    for (uint32_t i = first; i < first + count; ++i)
    {
        const Tri &t = _tris[_order[i]];
        bounds = bounds.joined(Bounds(glm::min(t.a, glm::min(t.b, t.c)), glm::max(t.a, glm::max(t.b, t.c))));
        const glm::vec3 &c = centroids[_order[i]];
        centroidBounds = centroidBounds.joined(Bounds(c, c));
    }
    _nodes[node].bounds = bounds;

    const glm::vec3 extent = centroidBounds.get_size();
    const int axis = extent.x > extent.y ? (extent.x > extent.z ? 0 : 2) : (extent.y > extent.z ? 1 : 2);
    if (count <= LeafSize || extent[axis] <= 0.0f)
    {
        _nodes[node].first = first;
        _nodes[node].count = count;
        return;
    }

    // Median split: balanced tree regardless of triangle distribution.
    const uint32_t mid = first + count / 2;
    std::nth_element(_order.begin() + first, _order.begin() + mid, _order.begin() + first + count,
                     [&](uint32_t a, uint32_t b) { return centroids[a][axis] < centroids[b][axis]; });

    const uint32_t left = uint32_t(_nodes.size());
    _nodes.emplace_back();
    _nodes.emplace_back();
    _nodes[node].first = left;
    _nodes[node].count = 0;
    build_node(left, first, mid - first, centroids);
    build_node(left + 1, mid, first + count - mid, centroids);
}

float TriangleBvh::aabb_distance_sq(const Bounds &b, const glm::vec3 &p)
{
    const glm::vec3 d = glm::max(glm::max(b.min - p, p - b.max), glm::vec3(0.0f));
    return glm::dot(d, d);
}

// Closest point on triangle (Ericson, Real-Time Collision Detection 5.1.5).
float TriangleBvh::point_triangle_distance_sq(const glm::vec3 &p, const Tri &t)
{
    const glm::vec3 ab = t.b - t.a, ac = t.c - t.a, ap = p - t.a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    glm::vec3 closest;
    if (d1 <= 0.0f && d2 <= 0.0f)
        closest = t.a;
    else
    {
        const glm::vec3 bp = p - t.b;
        const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
        const glm::vec3 cp = p - t.c;
        const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
        const float vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
        if (d3 >= 0.0f && d4 <= d3)
            closest = t.b;
        else if (d6 >= 0.0f && d5 <= d6)
            closest = t.c;
        else if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
            closest = t.a + ab * (d1 / (d1 - d3));
        else if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
            closest = t.a + ac * (d2 / (d2 - d6));
        else if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
            closest = t.b + (t.c - t.b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
        else
        {
            const float denom = 1.0f / (va + vb + vc);
            closest = t.a + ab * (vb * denom) + ac * (vc * denom);
        }
    }
    const glm::vec3 d = p - closest;
    return glm::dot(d, d);
}

float TriangleBvh::closest_distance(const glm::vec3 &p) const
{
    if (_nodes.empty())
        return std::numeric_limits<float>::infinity();
    float best = std::numeric_limits<float>::infinity();
    uint32_t stack[64];
    int top = 0;
    stack[top++] = 0;
    while (top > 0)
    {
        const Node &n = _nodes[stack[--top]];
        if (aabb_distance_sq(n.bounds, p) >= best)
            continue;
        if (n.count > 0)
        {
            for (uint32_t i = n.first; i < n.first + n.count; ++i)
                best = std::min(best, point_triangle_distance_sq(p, _tris[_order[i]]));
            continue;
        }
        // Visit the nearer child first so the far one is usually pruned.
        const uint32_t l = n.first, r = n.first + 1;
        const float dl = aabb_distance_sq(_nodes[l].bounds, p), dr = aabb_distance_sq(_nodes[r].bounds, p);
        if (top + 2 > 64)
            continue; // depth guard; a median-split tree never gets close to this
        if (dl < dr)
        {
            stack[top++] = r;
            stack[top++] = l;
        }
        else
        {
            stack[top++] = l;
            stack[top++] = r;
        }
    }
    return std::sqrt(best);
}

void TriangleBvh::axis_ray_hits(const glm::vec3 &origin, int axis, std::vector<float> &hits) const
{
    if (_nodes.empty())
        return;
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    uint32_t stack[64];
    int top = 0;
    stack[top++] = 0;
    while (top > 0)
    {
        const Node &n = _nodes[stack[--top]];
        const Bounds &b = n.bounds;
        if (origin[u] < b.min[u] || origin[u] > b.max[u] || origin[v] < b.min[v] || origin[v] > b.max[v] ||
            b.max[axis] < origin[axis])
            continue;
        if (n.count == 0)
        {
            if (top + 2 <= 64)
            {
                stack[top++] = n.first;
                stack[top++] = n.first + 1;
            }
            continue;
        }
        for (uint32_t i = n.first; i < n.first + n.count; ++i)
        {
            // Project onto the plane perpendicular to the ray and test the
            // point against the triangle with 2D edge functions.
            const Tri &t = _tris[_order[i]];
            const glm::vec2 p(origin[u], origin[v]);
            const glm::vec2 a(t.a[u], t.a[v]), bb(t.b[u], t.b[v]), c(t.c[u], t.c[v]);
            auto edge = [](const glm::vec2 &e0, const glm::vec2 &e1, const glm::vec2 &q) {
                return (e1.x - e0.x) * (q.y - e0.y) - (e1.y - e0.y) * (q.x - e0.x);
            };
            const float w0 = edge(bb, c, p), w1 = edge(c, a, p), w2 = edge(a, bb, p);
            const bool inside = (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) || (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            const float area = w0 + w1 + w2;
            if (!inside || area == 0.0f)
                continue;
            const float hit = (w0 * t.a[axis] + w1 * t.b[axis] + w2 * t.c[axis]) / area;
            if (hit > origin[axis])
                hits.push_back(hit - origin[axis]);
        }
    }
}
