#ifndef MATERIAL_WEIGHTS_H
#define MATERIAL_WEIGHTS_H

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>

// Blend weights of the 8 paintable materials of a voxel, 8 bits each, packed in
// 8 bytes. All zero means "not painted": the shader's procedural layers show.
struct MaterialWeights
{
    static constexpr int Count = 8;
    uint64_t packed = 0;

    uint8_t get(int i) const { return uint8_t(packed >> (8 * i)); }
    void set(int i, uint8_t w) { packed = (packed & ~(uint64_t(0xFF) << (8 * i))) | (uint64_t(w) << (8 * i)); }
    bool empty() const { return packed == 0; }

    static MaterialWeights one_hot(int material)
    {
        MaterialWeights m;
        m.set(std::clamp(material, 0, Count - 1), 255);
        return m;
    }

    // Weights 0..3 and 4..7 in [0, 1], as mesh vertex attributes.
    glm::vec4 low() const { return glm::vec4(get(0), get(1), get(2), get(3)) * (1.0f / 255.0f); }
    glm::vec4 high() const { return glm::vec4(get(4), get(5), get(6), get(7)) * (1.0f / 255.0f); }

    // Moves each weight a fraction t (0..1) of the way towards `target`.
    static MaterialWeights mix(const MaterialWeights &a, const MaterialWeights &target, float t)
    {
        MaterialWeights m;
        for (int i = 0; i < Count; ++i)
            m.set(i, uint8_t(std::clamp(a.get(i) + (float(target.get(i)) - a.get(i)) * t + 0.5f, 0.0f, 255.0f)));
        return m;
    }

    // Rounded average of 8 children.
    static MaterialWeights average(const MaterialWeights (&children)[8])
    {
        uint32_t sums[Count] = {};
        for (const MaterialWeights &child : children)
            for (int i = 0; i < Count; ++i)
                sums[i] += child.get(i);
        MaterialWeights m;
        for (int i = 0; i < Count; ++i)
            m.set(i, uint8_t((sums[i] + 4) / 8));
        return m;
    }
};

#endif // MATERIAL_WEIGHTS_H
