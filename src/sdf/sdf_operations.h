#ifndef SDF_OPERATIONS_H
#define SDF_OPERATIONS_H

#include <algorithm>
#include <glm/glm.hpp>

namespace SDF
{
enum Operation
{
    SDF_OPERATION_UNION,
    SDF_OPERATION_SUBTRACTION,
    SDF_OPERATION_INTERSECTION,
    SDF_OPERATION_SMOOTH_UNION,
    SDF_OPERATION_SMOOTH_SUBTRACTION,
    SDF_OPERATION_SMOOTH_INTERSECTION,
};

static inline float apply_operation(Operation op, float a, float b, float k = 1.0f)
{
    switch (op)
    {
    case SDF_OPERATION_UNION:
        return std::min(a, b);
    case SDF_OPERATION_SUBTRACTION:
        return std::max(a, -b);
    case SDF_OPERATION_INTERSECTION:
        return std::max(a, b);
    case SDF_OPERATION_SMOOTH_UNION:
    {
        float h = std::clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
        return glm::mix(b, a, h) - k * h * (1.0f - h);
    }
    case SDF_OPERATION_SMOOTH_SUBTRACTION:
    {
        float h = std::clamp(0.5f - 0.5f * (b + a) / k, 0.0f, 1.0f);
        return glm::mix(a, -b, h) + k * h * (1.0f - h);
    }
    case SDF_OPERATION_SMOOTH_INTERSECTION:
    {
        float h = std::clamp(0.5f - 0.5f * (b - a) / k, 0.0f, 1.0f);
        return glm::mix(b, a, h) + k * h * (1.0f - h);
    }
    default:
        return a;
    }
}

// Whether the normal of `a` counts in apply_operation below: it may cost much
// more to find than the operation itself (terrain normals were 70% of an edit),
// and where the operation replaces `a`, it does not.
static inline bool uses_first_normal(Operation op, float a, float b, float k)
{
    switch (op)
    {
    case SDF_OPERATION_UNION:
        return a < b;
    case SDF_OPERATION_SUBTRACTION:
        return a > -b;
    case SDF_OPERATION_INTERSECTION:
        return a > b;
    case SDF_OPERATION_SMOOTH_UNION:
        return 0.5f + 0.5f * (b - a) / k > 0.0f;
    case SDF_OPERATION_SMOOTH_SUBTRACTION:
        return 0.5f - 0.5f * (b + a) / k < 1.0f;
    case SDF_OPERATION_SMOOTH_INTERSECTION:
        return 0.5f - 0.5f * (b - a) / k > 0.0f;
    default:
        return true;
    }
}

static inline void apply_operation(
    Operation op,
    float a, const glm::vec3& na,
    float b, const glm::vec3& nb,
    float k,
    float& outValue, glm::vec3& outNormal)
{
    switch (op)
    {
    case SDF_OPERATION_UNION:
        if (a < b) { outValue = a; outNormal = na; }
        else       { outValue = b; outNormal = nb; }
        break;

    case SDF_OPERATION_SUBTRACTION:
        if (a > -b) { outValue = a; outNormal = na; }
        else        { outValue = -b; outNormal = -nb; }
        break;

    case SDF_OPERATION_INTERSECTION:
        if (a > b) { outValue = a; outNormal = na; }
        else       { outValue = b; outNormal = nb; }
        break;

    case SDF_OPERATION_SMOOTH_UNION:
    {
        float h = std::clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
        outValue = glm::mix(b, a, h) - k * h * (1.0f - h);
        outNormal = glm::normalize(glm::mix(nb, na, h));
    } break;

    case SDF_OPERATION_SMOOTH_SUBTRACTION:
    {
        float h = std::clamp(0.5f - 0.5f * (b + a) / k, 0.0f, 1.0f);
        outValue = glm::mix(a, -b, h) + k * h * (1.0f - h);
        outNormal = glm::normalize(glm::mix(na, -nb, h));
    } break;

    case SDF_OPERATION_SMOOTH_INTERSECTION:
    {
        float h = std::clamp(0.5f - 0.5f * (b - a) / k, 0.0f, 1.0f);
        outValue = glm::mix(b, a, h) + k * h * (1.0f - h);
        outNormal = glm::normalize(glm::mix(nb, na, h));
    } break;

    default:
        outValue = a;
        outNormal = na;
        break;
    }
}


} // namespace SDF

#endif // SDF_OPERATIONS_H