#include "voxel_lod.h"
#include "voxel_viewer.h"
#include "voxel_volume.h"
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <limits>

JarVoxelLoD::JarVoxelLoD(const bool automaticUpdate, const float automaticUpdateDistance, const int lodLevelCount,
                         const int shellSize, const float octreeScale)
    : _automaticUpdateDistance(automaticUpdateDistance), _octreeScale(octreeScale), _lodLevelCount(lodLevelCount),
      _shellSize(shellSize), _automaticUpdate(automaticUpdate)
{
}

std::vector<glm::vec3> JarVoxelLoD::gather_viewers(const JarVoxelVolume &volume)
{
    std::vector<glm::vec3> positions;
    for (const JarVoxelViewer *viewer : JarVoxelViewer::get_viewers())
    {
        if (!viewer->is_enabled() || !viewer->is_inside_tree())
            continue;
        const Vector3 p = volume.to_local(viewer->get_global_position());
        positions.emplace_back(p.x, p.y, p.z);
    }

    // No viewers: fall back to the active camera so a bare volume still works.
    if (positions.empty() && volume.is_inside_tree())
    {
        if (Viewport *viewport = volume.get_viewport())
            if (Camera3D *camera = viewport->get_camera_3d())
            {
                const Vector3 p = volume.to_local(camera->get_global_position());
                positions.emplace_back(p.x, p.y, p.z);
            }
    }
    return positions;
}

bool JarVoxelLoD::process(const JarVoxelVolume &volume)
{
    if (!_automaticUpdate)
        return false;
    return update_viewers(volume, false);
}

bool JarVoxelLoD::update_viewers(const JarVoxelVolume &volume, const bool force)
{
    if (volume.is_building())
        return false;

    std::vector<glm::vec3> positions = gather_viewers(volume);
    bool changed = force || positions.size() != _viewerPositions.size();
    for (size_t i = 0; !changed && i < positions.size(); ++i)
        changed = glm::distance(positions[i], _viewerPositions[i]) > _automaticUpdateDistance;

    if (changed)
        _viewerPositions = std::move(positions);
    return changed;
}

float JarVoxelLoD::distance_to_nearest_viewer(const glm::vec3 &position) const
{
    float best = std::numeric_limits<float>::infinity();
    for (const glm::vec3 &viewer : _viewerPositions)
        best = std::min(best, glm::distance(position, viewer));
    return best;
}

int JarVoxelLoD::desired_lod(const VoxelOctreeNode &node) const
{
    return node._size > _maxChunkSize ? 0 : lod_at(node._center);
}

float JarVoxelLoD::lod_to_grid_size(const int lod) const
{
    return (1 << (long)(lod + 1)) * _octreeScale;
}

glm::vec3 JarVoxelLoD::snap_to_grid(const glm::vec3 pos, const float grid_size) const
{
    return floor(pos / grid_size) * grid_size;
}

bool JarVoxelLoD::is_in_lod_shell(int lod, glm::vec3 pos, glm::vec3 cam_pos) const
{
    float grid_size = lod_to_grid_size(lod) * 2.0;
    glm::vec3 lod_cam_pos = snap_to_grid(cam_pos, grid_size);
    glm::vec3 delta = abs(pos - lod_cam_pos);
    float dist = glm::max(delta.x, glm::max(delta.y, delta.z));
    return dist < (grid_size * _shellSize);
}

int JarVoxelLoD::lod_for_viewer(const glm::vec3 &position, const glm::vec3 &viewer) const
{
    constexpr float rChunksize = 1.0f / 16.0f;
    glm::vec3 pos = position * rChunksize;
    glm::vec3 cam_pos = viewer * rChunksize;

    // log2 estimate of the shell, then correct the (at most one level) error.
    glm::vec3 delta = glm::abs(pos - cam_pos) / (2.0f * _shellSize);
    int lod = glm::max(0, int(floor(glm::log2(glm::max(1.0f, glm::max(delta.x, glm::max(delta.y, delta.z)))))));
    if (!is_in_lod_shell(lod, pos, cam_pos))
        return lod + 1;
    if (lod <= 0 || !is_in_lod_shell(lod - 1, pos, cam_pos))
        return lod;
    return lod - 1;
}

int JarVoxelLoD::lod_at(const glm::vec3 &position) const
{
    if (_viewerPositions.empty())
        return lod_for_viewer(position, glm::vec3(0.0f));
    int lod = std::numeric_limits<int>::max();
    for (const glm::vec3 &viewer : _viewerPositions)
        lod = std::min(lod, lod_for_viewer(position, viewer));
    return lod;
}
