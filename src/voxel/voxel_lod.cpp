#include "voxel_lod.h"
#include "voxel_viewer.h"
#include "voxel_volume.h"
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <limits>

JarVoxelLoD::JarVoxelLoD(bool automaticUpdate, float automaticUpdateDistance, int shellSize, float chunkMeters,
                         float detailDistance, int maxLod)
    : _automaticUpdateDistance(automaticUpdateDistance), _shellSize(std::max(1, shellSize)),
      _automaticUpdate(automaticUpdate), _chunkMeters(chunkMeters),
      _detailChunks(std::max(0.0f, detailDistance) / chunkMeters), _maxLod(std::max(0, maxLod))
{
    _maxChunkSize = _maxLod + JarVoxelVolume::ChunkSizeLog2;
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

// Positions in LOD 0 chunk units. A level's cube is centered on the viewer
// snapped to that level's grid, so its boundaries move in whole chunks.
bool JarVoxelLoD::is_in_lod_shell(int lod, const glm::vec3 &pos, const glm::vec3 &viewer) const
{
    const float grid = float(1 << (lod + 2));
    const glm::vec3 snapped = glm::floor(viewer / grid) * grid;
    const glm::vec3 delta = glm::abs(pos - snapped);
    const float dist = std::max(delta.x, std::max(delta.y, delta.z));
    return dist < grid * _shellSize + _detailChunks;
}

int JarVoxelLoD::lod_for_viewer(const glm::vec3 &position, const glm::vec3 &viewer) const
{
    const glm::vec3 pos = position / _chunkMeters;
    const glm::vec3 cam = viewer / _chunkMeters;

    // log2 estimate of the shell, then step to the exact level (snapping and the
    // detail offset can move it by a level or so).
    const glm::vec3 d = glm::abs(pos - cam);
    const float dist = std::max(0.0f, std::max(d.x, std::max(d.y, d.z)) - _detailChunks) / (2.0f * _shellSize);
    int lod = std::max(0, int(std::floor(std::log2(std::max(1.0f, dist)))));
    for (int i = 0; i < 4 && !is_in_lod_shell(lod, pos, cam); ++i)
        lod++;
    for (int i = 0; i < 4 && lod > 0 && is_in_lod_shell(lod - 1, pos, cam); ++i)
        lod--;
    return lod;
}

int JarVoxelLoD::lod_at(const glm::vec3 &position) const
{
    if (_viewerPositions.empty())
        return std::min(lod_for_viewer(position, glm::vec3(0.0f)), _maxLod);
    int lod = std::numeric_limits<int>::max();
    for (const glm::vec3 &viewer : _viewerPositions)
        lod = std::min(lod, lod_for_viewer(position, viewer));
    return std::min(lod, _maxLod);
}
