// Surface queries on a volume's field (see JarVoxelVolume::get_distance).
#include "voxel_octree_node.h"
#include "voxel_volume.h"
#include <godot_cpp/classes/character_body3d.hpp>
#include <godot_cpp/classes/rigid_body3d.hpp>
#include <cmath>
#include <limits>

namespace
{
constexpr float Unknown = std::numeric_limits<float>::quiet_NaN();

glm::vec3 to_glm(const Vector3 &v)
{
    return glm::vec3(v.x, v.y, v.z);
}

Vector3 to_godot(const glm::vec3 &v)
{
    return Vector3(v.x, v.y, v.z);
}
} // namespace

float JarVoxelVolume::field_at(const glm::vec3 &point)
{
    const JarSignedDistanceField *sdf = get_sdf_ptr();
    if (sdf == nullptr)
        return Unknown;
    if (!_everEdited || !_voxelRoot)
        return sdf->distance(point); // never edited: the SDF is the field
    if (_isBuilding)
        return Unknown; // the worker is changing the octree
    const glm::vec3 reach(2.0f * _voxelSize);
    if (!_voxelRoot->edited_around(_voxelSize, Bounds(point - reach, point + reach)))
        return sdf->distance(point);
    // Near an edit: the octree's samples, trilinear between the finest
    // voxels' centers ((k + 0.5) voxel sizes; coarser leaves fill theirs).
    const glm::vec3 grid = point / _voxelSize - 0.5f;
    const glm::vec3 base = glm::floor(grid);
    const glm::vec3 f = grid - base;
    float corner[8];
    for (int i = 0; i < 8; ++i)
    {
        const glm::vec3 offset(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        VoxelOctreeNode *leaf = _voxelRoot->node_at(_voxelSize, (base + offset + 0.5f) * _voxelSize);
        corner[i] = leaf != nullptr ? leaf->get_value() : sdf->distance((base + offset + 0.5f) * _voxelSize);
    }
    const float x00 = corner[0] + (corner[1] - corner[0]) * f.x;
    const float x10 = corner[2] + (corner[3] - corner[2]) * f.x;
    const float x01 = corner[4] + (corner[5] - corner[4]) * f.x;
    const float x11 = corner[6] + (corner[7] - corner[6]) * f.x;
    const float y0 = x00 + (x10 - x00) * f.y;
    const float y1 = x01 + (x11 - x01) * f.y;
    return y0 + (y1 - y0) * f.z;
}

glm::vec3 JarVoxelVolume::field_normal(const glm::vec3 &point)
{
    const JarSignedDistanceField *sdf = get_sdf_ptr();
    const glm::vec3 reach(2.0f * _voxelSize);
    if (sdf != nullptr && (!_everEdited || !_voxelRoot ||
                           (!_isBuilding && !_voxelRoot->edited_around(_voxelSize, Bounds(point - reach, point + reach)))))
        return sdf->normal(point);
    // Central differences over a voxel (the samples' spacing).
    const float h = _voxelSize;
    const glm::vec3 g(field_at(point + glm::vec3(h, 0, 0)) - field_at(point - glm::vec3(h, 0, 0)),
                      field_at(point + glm::vec3(0, h, 0)) - field_at(point - glm::vec3(0, h, 0)),
                      field_at(point + glm::vec3(0, 0, h)) - field_at(point - glm::vec3(0, 0, h)));
    const float length = glm::length(g);
    return std::isfinite(length) && length > 0.0f ? g / length : glm::vec3(Unknown);
}

float JarVoxelVolume::get_distance(const Vector3 &position)
{
    // Rigid: distances are the same in global and volume space.
    return field_at(to_glm(to_local(position)));
}

Dictionary JarVoxelVolume::project_to_surface(const Vector3 &position)
{
    // Newton steps along the field's gradient (a distance field: one step
    // is exact for a plane; terrain needs a few).
    glm::vec3 p = to_glm(to_local(position));
    glm::vec3 normal(0.0f);
    for (int step = 0; step < 6; ++step)
    {
        const float d = field_at(p);
        normal = field_normal(p);
        if (!std::isfinite(d) || !std::isfinite(normal.x))
            return Dictionary();
        p -= d * normal;
        if (std::abs(d) < 1e-3f * _voxelSize)
            break;
    }
    Dictionary result;
    result["position"] = to_global(to_godot(p));
    result["normal"] = (get_global_basis().xform(to_godot(normal))).normalized();
    return result;
}

Dictionary JarVoxelVolume::raycast(const Vector3 &from, const Vector3 &to)
{
    const glm::vec3 start = to_glm(to_local(from));
    const glm::vec3 delta = to_glm(to_local(to)) - start;
    const float length = glm::length(delta);
    const glm::vec3 dir = length > 0.0f ? delta / length : glm::vec3(0.0f, -1.0f, 0.0f);
    // Sphere tracing: the field never overstates the distance by much, so
    // steps of 90% of it do not pass the surface; a minimum step keeps it
    // moving along grazing rays. A step past the surface is bisected back.
    const float minStep = 0.05f * _voxelSize;
    float t = 0.0f;
    float lastT = 0.0f;
    float d = field_at(start);
    if (!std::isfinite(d))
        return Dictionary();
    float hitT = -1.0f;
    if (d <= 0.0f)
        hitT = 0.0f;
    for (int i = 0; i < 1024 && hitT < 0.0f && t < length; ++i)
    {
        lastT = t;
        t = std::min(length, t + std::max(0.9f * d, minStep));
        d = field_at(start + dir * t);
        if (!std::isfinite(d))
            return Dictionary();
        if (d <= 1e-3f * _voxelSize)
        {
            float lo = lastT, hi = t; // outside at lo, inside (or on) at hi
            for (int k = 0; k < 16; ++k)
            {
                const float mid = 0.5f * (lo + hi);
                (field_at(start + dir * mid) > 0.0f ? lo : hi) = mid;
            }
            hitT = hi;
        }
    }
    if (hitT < 0.0f)
        return Dictionary();
    const glm::vec3 hit = start + dir * hitT;
    const glm::vec3 normal = field_normal(hit);
    Dictionary result;
    result["position"] = to_global(to_godot(hit));
    result["normal"] = std::isfinite(normal.x) ? get_global_basis().xform(to_godot(normal)).normalized() : -to_godot(dir);
    result["distance"] = hitT;
    return result;
}

bool JarVoxelVolume::keep_above_surface(Node3D *body, float margin)
{
    if (body == nullptr || !body->is_inside_tree())
        return false;
    const glm::vec3 p = to_glm(to_local(body->get_global_position()));
    const float d = field_at(p);
    if (!std::isfinite(d) || d >= margin)
        return false;
    const glm::vec3 n = field_normal(p);
    if (!std::isfinite(n.x))
        return false;
    // Out along the field's gradient, to `margin` above the surface (one step
    // per physics tick: deep under rough terrain it may take a few).
    const glm::vec3 out = p + n * (margin - d);
    body->set_global_position(to_global(to_godot(out)));
    const Vector3 up = get_global_basis().xform(to_godot(n)).normalized();
    auto without_inward = [&](const Vector3 &v) {
        const real_t into = v.dot(up);
        return into < 0.0f ? v - up * into : v;
    };
    if (auto *rigid = Object::cast_to<RigidBody3D>(body))
        rigid->set_linear_velocity(without_inward(rigid->get_linear_velocity()));
    else if (auto *character = Object::cast_to<CharacterBody3D>(body))
        character->set_velocity(without_inward(character->get_velocity()));
    return true;
}
