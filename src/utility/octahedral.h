#ifndef JAR_OCTAHEDRAL_H
#define JAR_OCTAHEDRAL_H

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

// Unit vectors packed into 32 bits (2 x 16-bit octahedral coordinates).
// Precision is ~0.003 degrees, far below what meshing can resolve.
// 0 is reserved for "no normal".
namespace Octahedral
{
inline uint32_t encode(glm::vec3 n)
{
    n /= (std::abs(n.x) + std::abs(n.y) + std::abs(n.z));
    glm::vec2 e(n.x, n.y);
    if (n.z < 0.0f)
        e = (1.0f - glm::abs(glm::vec2(e.y, e.x))) * glm::vec2(e.x >= 0.0f ? 1.0f : -1.0f, e.y >= 0.0f ? 1.0f : -1.0f);
    const uint32_t x = uint32_t(std::lround((e.x * 0.5f + 0.5f) * 65534.0f)) + 1; // 1..65535
    const uint32_t y = uint32_t(std::lround((e.y * 0.5f + 0.5f) * 65534.0f)) + 1;
    return (y << 16) | x;
}

inline glm::vec3 decode(uint32_t packed)
{
    const glm::vec2 e(float((packed & 0xFFFF) - 1) / 65534.0f * 2.0f - 1.0f,
                      float((packed >> 16) - 1) / 65534.0f * 2.0f - 1.0f);
    glm::vec3 n(e.x, e.y, 1.0f - std::abs(e.x) - std::abs(e.y));
    const float t = std::max(-n.z, 0.0f);
    n.x += n.x >= 0.0f ? -t : t;
    n.y += n.y >= 0.0f ? -t : t;
    return glm::normalize(n);
}
} // namespace Octahedral

#endif // JAR_OCTAHEDRAL_H
