#ifndef MATERIAL_WEIGHTS_H
#define MATERIAL_WEIGHTS_H

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>

// Materials of a voxel: up to 4 (material id, weight) pairs out of 255
// materials, in 8 bytes. Weights are 0..255 and sum to at most 255; whatever is
// left is "unpainted": the shader's ground rules (grass, rock, sand...) show.
struct MaterialWeights
{
    static constexpr int Slots = 4;
    static constexpr int MaxMaterial = 254; // 255 marks "ground" in split meshes
    uint8_t ids[Slots] = {0, 0, 0, 0};
    uint8_t weights[Slots] = {0, 0, 0, 0}; // sorted, largest first

    bool empty() const { return weights[0] == 0; }
    int total() const { return weights[0] + weights[1] + weights[2] + weights[3]; }

    static MaterialWeights one_hot(int material)
    {
        MaterialWeights m;
        m.ids[0] = uint8_t(std::clamp(material, 0, MaxMaterial));
        m.weights[0] = 255;
        return m;
    }

    // Moves the weights a fraction t (0..1) of the way towards pure `material`.
    static MaterialWeights mix(const MaterialWeights &a, int material, float t);
    // Rounded average of 8 children (keeps the 4 largest).
    static MaterialWeights average(const MaterialWeights (&children)[8]);
};

// Accumulates weighted material sets (interpolation, averaging) and reduces
// them back to the strongest few.
struct MaterialAccumulator
{
    static constexpr int Capacity = 32;
    uint8_t ids[Capacity];
    float weights[Capacity];
    int count = 0;

    void add(int id, float w)
    {
        if (w <= 0.0f)
            return;
        for (int i = 0; i < count; ++i)
            if (ids[i] == id)
            {
                weights[i] += w;
                return;
            }
        if (count < Capacity)
        {
            ids[count] = uint8_t(id);
            weights[count++] = w;
        }
    }
    // Adds a voxel's set, its weights (0..255) scaled by `scale`.
    void add(const MaterialWeights &m, float scale)
    {
        for (int i = 0; i < MaterialWeights::Slots && m.weights[i] > 0; ++i)
            add(m.ids[i], m.weights[i] * scale);
    }
    void scale(float s)
    {
        for (int i = 0; i < count; ++i)
            weights[i] *= s;
    }
    void sort()
    {
        // Insertion sort, largest first: count is tiny.
        for (int i = 1; i < count; ++i)
            for (int j = i; j > 0 && weights[j] > weights[j - 1]; --j)
            {
                std::swap(weights[j], weights[j - 1]);
                std::swap(ids[j], ids[j - 1]);
            }
    }
    float weight_of(int id) const
    {
        for (int i = 0; i < count; ++i)
            if (ids[i] == id)
                return weights[i];
        return 0.0f;
    }
    float total() const
    {
        float t = 0.0f;
        for (int i = 0; i < count; ++i)
            t += weights[i];
        return t;
    }
    // The 4 strongest (weights 0..255). Dropped materials hand their weight to
    // the kept ones, so a blend of many materials does not turn into ground.
    MaterialWeights strongest() const
    {
        MaterialAccumulator sorted = *this;
        sorted.sort();
        MaterialWeights m;
        const int n = std::min(count, MaterialWeights::Slots);
        float kept = 0.0f;
        for (int i = 0; i < n; ++i)
            kept += sorted.weights[i];
        const float boost = kept > 0.0f ? std::min(255.0f, sorted.total()) / kept : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            m.ids[i] = sorted.ids[i];
            m.weights[i] = uint8_t(std::clamp(sorted.weights[i] * boost + 0.5f, 0.0f, 255.0f));
        }
        return m;
    }
};

// Materials at a mesh vertex: the strongest 4 ids with weights 0..1 (summing
// to at most 1; the rest is ground).
struct VertexMaterials
{
    uint8_t ids[4] = {0, 0, 0, 0};
    float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float painted() const { return weights[0] + weights[1] + weights[2] + weights[3]; }
    float weight_of(int id) const
    {
        for (int i = 0; i < 4; ++i)
            if (ids[i] == id)
                return weights[i];
        return 0.0f;
    }
    // From an accumulator holding weights in 0..255 units.
    static VertexMaterials from(const MaterialAccumulator &acc)
    {
        MaterialAccumulator sorted = acc;
        sorted.sort();
        VertexMaterials v;
        for (int i = 0; i < std::min(sorted.count, 4); ++i)
        {
            v.ids[i] = sorted.ids[i];
            v.weights[i] = std::min(1.0f, sorted.weights[i] / 255.0f);
        }
        return v;
    }
};

inline MaterialWeights MaterialWeights::mix(const MaterialWeights &a, int material, float t)
{
    MaterialAccumulator acc;
    acc.add(a, 1.0f - t);
    acc.add(std::clamp(material, 0, MaxMaterial), 255.0f * t);
    return acc.strongest();
}

inline MaterialWeights MaterialWeights::average(const MaterialWeights (&children)[8])
{
    MaterialAccumulator acc;
    for (const MaterialWeights &child : children)
        acc.add(child, 0.125f);
    return acc.strongest();
}

#endif // MATERIAL_WEIGHTS_H
