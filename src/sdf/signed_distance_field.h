#ifndef JAR_SIGNED_DISTANCE_FIELD_H
#define JAR_SIGNED_DISTANCE_FIELD_H

#include <algorithm>
#include <glm/glm.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <optional>
#include "sdf_operations.h"

using namespace godot;

class JarSignedDistanceField : public Resource
{


    GDCLASS(JarSignedDistanceField, Resource);

  public:
    virtual ~JarSignedDistanceField() = default;

    std::optional<glm::vec3> ray_march(const glm::vec3 &from, const glm::vec3 &dir, float epsilon = 0.01f,
                                       int maxSteps = 100) const
    {
        glm::vec3 currentPos = from;
        for (int i = 0; i < maxSteps; i++)
        {
            float d = distance(currentPos);
            if (std::abs(d) < epsilon)
                return currentPos;
            currentPos += dir * d;
        }
        return std::nullopt;
    }

    inline glm::vec3 normal(const glm::vec3 &pos) const
    {
        const static float delta = 0.001f;
        const static glm::vec3 xyy(delta, -delta, -delta);
        const static glm::vec3 yyx(-delta, -delta, delta);
        const static glm::vec3 yxy(-delta, delta, -delta);
        const static glm::vec3 xxx(delta, delta, delta);
        return glm::normalize(xyy * distance(pos + xyy) + yyx * distance(pos + yyx) + yxy * distance(pos + yxy) +
                              xxx * distance(pos + xxx));
    }

    virtual float distance(const glm::vec3 &pos) const = 0;

    // GDScript-friendly wrapper: query the field at a world position.
    // Negative = inside the surface, positive = outside, 0 = on it.
    float sample(const Vector3 &pos) const
    {
        return distance(glm::vec3(pos.x, pos.y, pos.z));
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("sample", "position"), &JarSignedDistanceField::sample);
    }
};

#endif // JAR_SIGNED_DISTANCE_FIELD_H
