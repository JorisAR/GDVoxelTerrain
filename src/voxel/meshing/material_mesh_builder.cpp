#include "material_mesh_builder.h"
#include <unordered_map>

namespace
{
uint8_t to_byte(float v)
{
    return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// Remaps vertices per (vertex, key): the first triangle using a vertex with a
// key keeps the original, others get copies.
struct VertexSplitter
{
    PackedVector3Array &verts;
    PackedVector3Array &normals;
    std::vector<int> sourceOf; // output vertex -> input vertex
    std::unordered_map<uint64_t, int> variants;
    std::vector<uint32_t> firstKey;

    VertexSplitter(PackedVector3Array &v, PackedVector3Array &n) : verts(v), normals(n)
    {
        sourceOf.resize(v.size());
        for (int i = 0; i < int(sourceOf.size()); ++i)
            sourceOf[i] = i;
        firstKey.assign(v.size(), 0xFFFFFFFFu);
    }

    int get(int v, uint32_t key)
    {
        if (firstKey[v] == 0xFFFFFFFFu || firstKey[v] == key)
        {
            firstKey[v] = key;
            return v;
        }
        const uint64_t k = (uint64_t(v) << 32) | key;
        auto it = variants.find(k);
        if (it != variants.end())
            return it->second;
        const int copy = verts.size();
        verts.push_back(verts[v]);
        normals.push_back(normals[v]);
        sourceOf.push_back(v);
        variants.emplace(k, copy);
        return copy;
    }
};
} // namespace

void MaterialMeshBuilder::build(MaterialMode mode, PackedVector3Array &verts, PackedVector3Array &normals,
                                PackedInt32Array &indices, const std::vector<VertexMaterials> &materials,
                                PackedColorArray &colors, PackedByteArray &custom0, PackedFloat32Array &painted)
{
    colors.clear();
    custom0.clear();
    painted.clear();

    if (mode == MaterialMode::Weights)
    {
        // Ids 0-7 become fixed weight channels; anything above 7 is ignored.
        colors.resize(verts.size());
        custom0.resize(verts.size() * 4);
        painted.resize(verts.size());
        for (int64_t v = 0; v < verts.size(); ++v)
        {
            float w[8] = {};
            for (int i = 0; i < 4; ++i)
                if (materials[v].ids[i] < 8)
                    w[materials[v].ids[i]] += materials[v].weights[i];
            colors.set(v, Color(w[0], w[1], w[2], w[3]));
            for (int i = 0; i < 4; ++i)
                custom0.set(4 * v + i, to_byte(w[4 + i]));
            painted.set(v, w[0] + w[1] + w[2] + w[3] + w[4] + w[5] + w[6] + w[7]);
        }
        return;
    }

    VertexSplitter splitter(verts, normals);
    struct Corner
    {
        uint8_t ids[4];
        float weights[4];
    };
    std::vector<Corner> corners; // per output vertex, filled below
    auto set_corner = [&](int out, const Corner &c) {
        if (int(corners.size()) <= out)
            corners.resize(out + 1);
        corners[out] = c;
    };

    for (int64_t t = 0; t + 2 < indices.size(); t += 3)
    {
        const int v[3] = {indices[t], indices[t + 1], indices[t + 2]};
        Corner c[3] = {};
        uint32_t key = 0;
        if (mode == MaterialMode::Split)
        {
            // The triangle's strongest material, if painted at least halfway.
            MaterialAccumulator acc;
            for (int k = 0; k < 3; ++k)
                for (int i = 0; i < 4; ++i)
                    acc.add(materials[v[k]].ids[i], materials[v[k]].weights[i]);
            acc.sort();
            float paintedSum = 0.0f;
            for (int k = 0; k < 3; ++k)
                paintedSum += materials[v[k]].painted();
            const uint8_t id = acc.count > 0 && paintedSum >= 1.5f ? acc.ids[0] : Ground;
            for (int k = 0; k < 3; ++k)
            {
                c[k].ids[0] = id;
                c[k].weights[0] = id == Ground ? 0.0f : 1.0f;
            }
            key = id;
        }
        else
        {
            // The 4 materials strongest over the triangle; each corner weighs
            // them by its own data, so blends stay smooth inside the triangle
            // and every vertex of it agrees on which materials they are.
            MaterialAccumulator acc;
            for (int k = 0; k < 3; ++k)
                for (int i = 0; i < 4; ++i)
                    acc.add(materials[v[k]].ids[i], materials[v[k]].weights[i]);
            acc.sort();
            uint8_t ids[4] = {0, 0, 0, 0};
            const int n = std::min(acc.count, 4);
            for (int i = 0; i < n; ++i)
                ids[i] = acc.ids[i];
            // Unused slots repeat a used id with weight 0 (keeps the key stable).
            for (int i = n; i < 4; ++i)
                ids[i] = n > 0 ? ids[0] : 0;
            // Sorted ids: the same set in any order shares vertices.
            std::sort(ids, ids + 4);
            for (int i = 0; i < 4; ++i)
                key = key * 256u + ids[i];
            for (int k = 0; k < 3; ++k)
                for (int i = 0; i < 4; ++i)
                {
                    c[k].ids[i] = ids[i];
                    // A repeated id only counts once.
                    bool repeat = false;
                    for (int j = 0; j < i; ++j)
                        repeat = repeat || ids[j] == ids[i];
                    c[k].weights[i] = repeat ? 0.0f : materials[v[k]].weight_of(ids[i]);
                }
        }
        for (int k = 0; k < 3; ++k)
        {
            const int out = splitter.get(v[k], key);
            indices.set(t + k, out);
            set_corner(out, c[k]);
        }
    }

    colors.resize(verts.size());
    custom0.resize(verts.size() * 4);
    painted.resize(verts.size());
    corners.resize(verts.size());
    for (int64_t v = 0; v < verts.size(); ++v)
    {
        const Corner &c = corners[v];
        if (mode == MaterialMode::Split)
        {
            colors.set(v, Color(1, 1, 1, 1));
            custom0.set(4 * v, c.ids[0]);
            custom0.set(4 * v + 1, 0);
            custom0.set(4 * v + 2, 0);
            custom0.set(4 * v + 3, 0);
            painted.set(v, c.weights[0]);
        }
        else
        {
            colors.set(v, Color(c.weights[0], c.weights[1], c.weights[2], c.weights[3]));
            for (int i = 0; i < 4; ++i)
                custom0.set(4 * v + i, c.ids[i]);
            painted.set(v, c.weights[0] + c.weights[1] + c.weights[2] + c.weights[3]);
        }
    }
}
