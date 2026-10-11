#ifndef MODIFY_SETTINGS_H
#define MODIFY_SETTINGS_H

#include "signed_distance_field.h"
#include "bounds.h"
#include "sdf_operations.h"

struct ModifySettings
{
    public:
        Ref<JarSignedDistanceField> sdf;
        Bounds bounds;
        glm::vec3 position;
        SDF::Operation operation;
        // Material index (0..3) painted onto voxels this edit touches. Becomes a
        // one-hot weight in the node color, which the mesher interpolates into
        // vertex COLOR and the chunk shader blends as materials. -1 = leave the
        // existing material weights untouched (e.g. dig without repainting).
        int material = 0;
};

#endif // MODIFY_SETTINGS_H
