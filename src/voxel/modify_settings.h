#ifndef MODIFY_SETTINGS_H
#define MODIFY_SETTINGS_H

#include "bounds.h"
#include "sdf_operations.h"
#include "signed_distance_field.h"
#include <godot_cpp/variant/transform3d.hpp>

inline glm::mat4 to_glm(const Transform3D &t)
{
    const Basis &b = t.basis;
    return glm::mat4(glm::vec4(b.rows[0].x, b.rows[1].x, b.rows[2].x, 0.0f),
                     glm::vec4(b.rows[0].y, b.rows[1].y, b.rows[2].y, 0.0f),
                     glm::vec4(b.rows[0].z, b.rows[1].z, b.rows[2].z, 0.0f),
                     glm::vec4(t.origin.x, t.origin.y, t.origin.z, 1.0f));
}

// One queued edit, fully resolved into the volume's local space.
struct ModifySettings
{
    Ref<JarSignedDistanceField> sdf;
    Bounds bounds;                      // volume-local region the edit may change
    glm::mat4 toSdf{1.0f};              // volume-local point -> SDF-local point
    float distanceScale = 1.0f;         // SDF-local distance -> volume-local distance
    SDF::Operation operation = SDF::SDF_OPERATION_UNION;
    float smoothness = 0.0f;            // blend radius for smooth ops; <= 0 uses the voxel size
    // Material index (0..3) painted onto voxels this edit touches. Becomes a
    // one-hot weight in the node color, which the mesher interpolates into
    // vertex COLOR and the chunk shader blends as materials. -1 = leave the
    // existing material weights untouched (e.g. dig without repainting).
    int material = 0;

    float sample(const glm::vec3 &volumePoint) const
    {
        return sdf->distance(glm::vec3(toSdf * glm::vec4(volumePoint, 1.0f))) * distanceScale;
    }
};

#endif // MODIFY_SETTINGS_H
