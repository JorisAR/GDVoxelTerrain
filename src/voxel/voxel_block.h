#ifndef JAR_VOXEL_BLOCK_H
#define JAR_VOXEL_BLOCK_H

#include "bounds.h"
#include "material_weights.h"
#include "octahedral.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

// A dense box of voxels at one spacing: signed distance, materials and the
// surface normal (Octahedral-packed, 0 = none) per cell. A copy of part of a
// volume (JarVoxelVolume::sample_region), or a piece cut out of one. Cell
// (x, y, z) sits at origin + (x, y, z) * spacing; cells outside the box count
// as empty space.
struct VoxelBlock
{
    glm::ivec3 size{0};
    float spacing = 1.0f;
    glm::vec3 origin{0.0f}; // position of cell (0, 0, 0)
    std::vector<float> values;
    std::vector<MaterialWeights> materials;
    std::vector<uint32_t> normals;

    void resize(const glm::ivec3 &cells)
    {
        size = glm::max(cells, glm::ivec3(0));
        const size_t n = size_t(size.x) * size_t(size.y) * size_t(size.z);
        values.assign(n, spacing);
        materials.assign(n, MaterialWeights());
        normals.assign(n, 0u);
    }
    int64_t cell_count() const { return int64_t(size.x) * size.y * size.z; }
    bool contains(const glm::ivec3 &c) const
    {
        return c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x < size.x && c.y < size.y && c.z < size.z;
    }
    int64_t index(const glm::ivec3 &c) const { return c.x + int64_t(size.x) * (c.y + int64_t(size.y) * c.z); }
    glm::ivec3 cell(int64_t i) const
    {
        return glm::ivec3(int(i % size.x), int((i / size.x) % size.y), int(i / (int64_t(size.x) * size.y)));
    }
    glm::vec3 position(const glm::ivec3 &c) const { return origin + glm::vec3(c) * spacing; }
    // The cell nearest to a position (may lie outside the box).
    glm::ivec3 nearest_cell(const glm::vec3 &p) const
    {
        return glm::ivec3(glm::floor((p - origin) / spacing + 0.5f));
    }
    // Region covered by the cells' centers.
    Bounds bounds() const { return Bounds(origin, origin + glm::vec3(glm::max(size - 1, glm::ivec3(0))) * spacing); }

    // Empty space (one cell of distance) outside the box.
    float value_at(const glm::ivec3 &c) const { return contains(c) ? values[index(c)] : spacing; }
    MaterialWeights materials_at(const glm::ivec3 &c) const { return contains(c) ? materials[index(c)] : MaterialWeights(); }

    // Trilinear distance at a position; beyond the box, the distance to it
    // (plus a cell, the box's empty border).
    float sample(const glm::vec3 &p) const
    {
        const glm::vec3 g = (p - origin) / spacing;
        const glm::vec3 hi = glm::vec3(glm::max(size - 1, glm::ivec3(0)));
        const glm::vec3 inside = glm::clamp(g, glm::vec3(0.0f), hi);
        const float outside = glm::length(g - inside) * spacing;
        const glm::ivec3 base = glm::min(glm::ivec3(glm::floor(inside)), glm::max(size - 2, glm::ivec3(0)));
        const glm::vec3 f = glm::clamp(inside - glm::vec3(base), glm::vec3(0.0f), glm::vec3(1.0f));
        float v = 0.0f;
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::ivec3 o(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
            const float w = (o.x ? f.x : 1.0f - f.x) * (o.y ? f.y : 1.0f - f.y) * (o.z ? f.z : 1.0f - f.z);
            v += w * value_at(base + o);
        }
        return outside > 0.0f ? std::max(v, 0.0f) + outside : v;
    }

    // Surface normal at a cell: the stored one, else central differences.
    glm::vec3 normal_at(const glm::ivec3 &c) const
    {
        if (contains(c) && normals[index(c)] != 0u)
            return Octahedral::decode(normals[index(c)]);
        const glm::vec3 g(value_at(c + glm::ivec3(1, 0, 0)) - value_at(c - glm::ivec3(1, 0, 0)),
                          value_at(c + glm::ivec3(0, 1, 0)) - value_at(c - glm::ivec3(0, 1, 0)),
                          value_at(c + glm::ivec3(0, 0, 1)) - value_at(c - glm::ivec3(0, 0, 1)));
        const float length = glm::length(g);
        return length > 1e-6f ? g / length : glm::vec3(0.0f, 1.0f, 0.0f);
    }
};

#endif // JAR_VOXEL_BLOCK_H
