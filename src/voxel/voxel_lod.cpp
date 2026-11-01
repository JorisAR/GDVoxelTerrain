#include <godot_cpp/classes/editor_interface.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/sub_viewport.hpp>
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
    // Editor preview: the editor's 3D camera is the only viewer.
    if (Engine::get_singleton()->is_editor_hint())
    {
        if (EditorInterface *editor = EditorInterface::get_singleton())
            if (SubViewport *viewport = editor->get_editor_viewport_3d(0))
                if (Camera3D *camera = viewport->get_camera_3d())
                {
                    const Vector3 p = volume.to_local(camera->get_global_position());
                    positions.emplace_back(p.x, p.y, p.z);
                }
        return positions;
    }
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
    const float half = 0.5f * volume.get_extent();
    std::vector<glm::ivec4> cells;
    cells.reserve(positions.size());
    for (const glm::vec3 &p : positions)
        cells.push_back(shell_cell(p, half));
    // Rebuild only when a shell moves: a build that changes no LOD still
    // walked the octree (~70 ms) and held up edits meanwhile; half of them
    // did, moving at walking speed. And not before the viewer moved the
    // update distance, so going back and forth over a grid line does not
    // rebuild each time.
    bool changed = force || positions.size() != _viewerPositions.size();
    if (!changed && cells != _viewerCells)
        for (size_t i = 0; !changed && i < positions.size(); ++i)
            changed = glm::distance(positions[i], _viewerPositions[i]) > _automaticUpdateDistance;

    if (changed)
    {
        _viewerPositions = std::move(positions);
        _viewerCells = std::move(cells);
    }
    return changed;
}

glm::ivec4 JarVoxelLoD::shell_cell(const glm::vec3 &viewer, float halfExtent) const
{
    // A viewer outside the volume's cube only reaches it with coarser levels
    // (a moon seen from its planet): their grids, not the finest, decide when
    // its shells move. One level finer than the estimate, to be safe.
    const glm::vec3 nearest = glm::clamp(viewer, glm::vec3(-halfExtent), glm::vec3(halfExtent));
    const int lod = nearest == viewer ? 0 : std::clamp(lod_for_viewer(nearest, viewer) - 1, 0, _maxLod);
    const float grid = float(1 << (lod + 2)) * _chunkMeters;
    return glm::ivec4(glm::ivec3(glm::floor(viewer / grid)), lod);
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
    int lod = std::numeric_limits<int>::max();
    if (_viewerPositions.empty())
        lod = lod_for_viewer(position, glm::vec3(0.0f));
    for (const glm::vec3 &viewer : _viewerPositions)
        lod = std::min(lod, lod_for_viewer(position, viewer));
    return std::min(std::max(lod, _minLod), _maxLod);
}
