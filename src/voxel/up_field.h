#ifndef JAR_UP_FIELD_H
#define JAR_UP_FIELD_H

#include <glm/glm.hpp>

// Which way is "up" inside a volume, and how high a point is, in the volume's
// local space. Used by population rules (slope, height range) and exposed to
// scripts for player controllers. Plain data, safe to copy into worker threads.
struct UpField
{
    enum Mode
    {
        AXIS = 0,   // flat world: up is a fixed direction
        RADIAL = 1, // planet: up points away from a center
    };

    Mode mode = AXIS;
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    glm::vec3 center{0.0f};
    // Height 0: distance along the axis (AXIS) or radius from center (RADIAL).
    float sea_level = 0.0f;

    glm::vec3 up(const glm::vec3 &p) const
    {
        if (mode == RADIAL)
        {
            const glm::vec3 d = p - center;
            const float len = glm::length(d);
            return len > 1e-6f ? d / len : axis;
        }
        return axis;
    }

    float height(const glm::vec3 &p) const
    {
        if (mode == RADIAL)
            return glm::length(p - center) - sea_level;
        return glm::dot(p - center, axis) - sea_level;
    }
};

#endif // JAR_UP_FIELD_H
