#include "voxel_lod.h"
#include "voxel_terrain.h"
#include "mesh_compute_scheduler.h"
#include <cmath>
using namespace godot;

JarVoxelLoD::JarVoxelLoD()
    : _automaticUpdate(true), _automaticUpdateDistance(32.0f), _lodLevelCount(20),
      _maxChunkSize(4), _autoMeshCoolDown(0.0f), _cameraPosition(0.0f, 0.0f, 0.0f)
{
}

JarVoxelLoD::JarVoxelLoD(const bool automaticUpdate, const float automaticUpdateDistance, const int lodLevelCount, const int shellSize, const float octreeScale, const int chunk_size)
    : _automaticUpdate(automaticUpdate), _automaticUpdateDistance(automaticUpdateDistance), _lodLevelCount(lodLevelCount), _shellSize(shellSize), _octreeScale(octreeScale),
      _autoMeshCoolDown(0.0f), _cameraPosition(0.0f, 0.0f, 0.0f), _rChunkSize(1.0f / chunk_size)
{
    int safe_chunk_size = std::max(chunk_size, 1);
    int chunk_size_log2 = static_cast<int>(std::round(std::log2(static_cast<float>(safe_chunk_size))));
    _maxChunkSize = std::max(_lodLevelCount - 1, 1) + std::max(chunk_size_log2, 0);
}

glm::vec3 JarVoxelLoD::get_camera_position() const
{
    return _cameraPosition;
}

bool JarVoxelLoD::process(const JarVoxelTerrain &terrain, double delta)
{
    _autoMeshCoolDown -= static_cast<float>(delta);
    if (!_automaticUpdate)
        return false;
    return update_camera_position(terrain, false);
}

bool JarVoxelLoD::update_camera_position(const JarVoxelTerrain &terrain, const bool force)
{
    if (terrain.is_building())
        return false;
    auto player = terrain.get_player_node();
    if (player == nullptr)
        return false;

    auto p = player->get_global_transform().origin - terrain.get_global_position();
    glm::vec3 glmp = {p.x, p.y, p.z};

    if (force || (glm::distance(_cameraPosition, glmp) > _automaticUpdateDistance))
    {
        _cameraPosition = glmp;
        return true;
    }
    return false;
}

int JarVoxelLoD::desired_lod(const VoxelOctreeNode &node)
{
    auto l = node.get_size_log2() > _maxChunkSize ? 0 : lod_at(node.get_center());
    return l;
}

inline float JarVoxelLoD::lod_to_grid_size(const int lod) const
{
    return (1 << (long)(lod + 1)) * _octreeScale; // should be times octreescale?
}

inline glm::vec3 JarVoxelLoD::snap_to_grid(const glm::vec3 pos, const float grid_size) const
{
    return floor(pos / grid_size) * grid_size;
}

inline bool JarVoxelLoD::is_in_lod_shell(int lod, glm::vec3 pos, glm::vec3 cam_pos) const
{
    float grid_size = lod_to_grid_size(lod) * 2.0;
    glm::vec3 lod_cam_pos = snap_to_grid(cam_pos, grid_size);
    glm::vec3 delta = abs(pos - lod_cam_pos);
    float dist = glm::max(delta.x, glm::max(delta.y, delta.z));
    return dist < (grid_size * _shellSize);
}

inline int JarVoxelLoD::lod_at(const glm::vec3 &position) const
{
    glm::vec3 pos = position * _rChunkSize;
    glm::vec3 cam_pos = _cameraPosition * _rChunkSize;

    // OLD: use for loop
    //  for (int lod = 0; lod < _lodLevelCount; ++lod) {
    //      if (is_in_lod_shell(lod, pos, cam_pos)) {
    //          return lod;
    //      }
    //  }
    //  return - 1; // Fallback to the largest LOD

    // NEW: use logarithm + adjustment.
    glm::vec3 delta = glm::abs(pos - cam_pos) / (2.0f * _shellSize);
    int lod = glm::max(0, int(floor(glm::log2(glm::max(1.0f, glm::max(delta.x, glm::max(delta.y, delta.z)))))));
    // Approximation is at most 1 off, so check if it should be +1 or -1.
    if (!is_in_lod_shell(lod, pos, cam_pos))
        return lod + 1;
    if (lod <= 0 || !is_in_lod_shell(lod - 1, pos, cam_pos))
        return lod;
    return lod - 1;
}
