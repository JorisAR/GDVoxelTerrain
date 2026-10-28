#ifndef JAR_SIGNED_DISTANCE_FIELD_H
#define JAR_SIGNED_DISTANCE_FIELD_H

#include "bounds.h"
#include "sdf_operations.h"
#include "up_field.h"
#include <algorithm>
#include <glm/glm.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <optional>

using namespace godot;

// Base class of all SDF resources. distance() and bounds() are evaluated on
// worker threads, so they must be const and thread-safe. Anything expensive to
// set up (e.g. baking a mesh) belongs in prepare(), which runs on the main
// thread before a volume or an edit uses the field.
class JarSignedDistanceField : public Resource
{
    GDCLASS(JarSignedDistanceField, Resource);

  public:
    virtual ~JarSignedDistanceField() = default;

    // Signed distance in the field's local space: negative inside.
    virtual float distance(const glm::vec3 &pos) const = 0;

    // Local-space box enclosing everything where distance() <= 0, or
    // Bounds::infinite() for unbounded fields (planes, terrain). Edits use it to
    // limit the region they touch.
    virtual Bounds bounds() const
    {
        return Bounds::infinite();
    }

    // Main thread: build caches needed by distance(). Containers forward it to
    // their children.
    // Shapes with an obvious "up" (terrain, planets) report it here, so volumes
    // in UP_MODE_AUTO need no separate up / sea level settings.
    virtual bool natural_up(UpField &out) const
    {
        return false;
    }

    virtual void prepare()
    {
    }

    // Unnormalized gradient of distance() (tetrahedral differences). Its length
    // is 1 for exact distance fields; noise-displaced fields deviate, which
    // dual contouring corrects for.
    virtual glm::vec3 gradient(const glm::vec3 &pos) const
    {
        // Step relative to the coordinate magnitude: far from the origin (e.g. a
        // 4 km planet) a fixed tiny step drowns in float rounding.
        const float delta = std::max(1e-3f, 1e-5f * std::max(std::abs(pos.x), std::max(std::abs(pos.y), std::abs(pos.z))));
        const glm::vec3 xyy(delta, -delta, -delta);
        const glm::vec3 yyx(-delta, -delta, delta);
        const glm::vec3 yxy(-delta, delta, -delta);
        const glm::vec3 xxx(delta, delta, delta);
        const glm::vec3 sum = xyy * distance(pos + xyy) + yyx * distance(pos + yyx) + yxy * distance(pos + yxy) +
                              xxx * distance(pos + xxx);
        return sum / (4.0f * delta * delta);
    }

    virtual glm::vec3 normal(const glm::vec3 &pos) const
    {
        const glm::vec3 g = gradient(pos);
        const float len = glm::length(g);
        return len > 0.0f ? g / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }

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

    // GDScript-friendly wrappers.
    // Signed distance at a point in the field's local space (negative = inside).
    float sample(const Vector3 &pos) const
    {
        return distance(glm::vec3(pos.x, pos.y, pos.z));
    }
    // Estimated surface normal (distance gradient) at a local point.
    Vector3 sample_normal(const Vector3 &pos) const
    {
        const glm::vec3 n = normal(glm::vec3(pos.x, pos.y, pos.z));
        return Vector3(n.x, n.y, n.z);
    }
    // Local bounds of the solid part; empty AABB if unbounded.
    AABB get_bounds() const
    {
        const Bounds b = bounds();
        if (!b.is_valid() || b.is_infinite())
            return AABB();
        const glm::vec3 size = b.get_size();
        return AABB(Vector3(b.min.x, b.min.y, b.min.z), Vector3(size.x, size.y, size.z));
    }
    bool is_bounded() const
    {
        const Bounds b = bounds();
        return b.is_valid() && !b.is_infinite();
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("sample", "position"), &JarSignedDistanceField::sample);
        ClassDB::bind_method(D_METHOD("sample_normal", "position"), &JarSignedDistanceField::sample_normal);
        ClassDB::bind_method(D_METHOD("get_bounds"), &JarSignedDistanceField::get_bounds);
        ClassDB::bind_method(D_METHOD("is_bounded"), &JarSignedDistanceField::is_bounded);
    }

    // Setter helper: store a value and notify dependents (volumes, parents).
    template <typename T> void set_and_notify(T &field, const T &value)
    {
        field = value;
        settings_changed();
        emit_changed();
    }

    // Called by set_and_notify before dependents hear of a change (e.g. to drop
    // cached values).
    virtual void settings_changed()
    {
    }
};

#endif // JAR_SIGNED_DISTANCE_FIELD_H
