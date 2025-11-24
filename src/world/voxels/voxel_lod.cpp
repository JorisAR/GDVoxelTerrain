#include "voxel_lod.h"
#include "voxel_terrain.h"
#include "mesh_compute_scheduler.h"
using namespace godot;

JarVoxelLoD::JarVoxelLoD()
    : _automaticUpdate(true), _automaticUpdateDistance(32.0f), _lodLevelCount(20),
       _autoMeshCoolDown(0.0f), _cameraPosition(0.0f, 0.0f, 0.0f)
{
}

JarVoxelLoD::JarVoxelLoD(const bool automaticUpdate, const float automaticUpdateDistance, const int lodLevelCount, const int shellSize, const float octreeScale, const int chunk_size)
    : _automaticUpdate(automaticUpdate), _automaticUpdateDistance(automaticUpdateDistance), _lodLevelCount(lodLevelCount), _shellSize(shellSize), _octreeScale(octreeScale),
       _autoMeshCoolDown(0.0f), _cameraPosition(0.0f, 0.0f, 0.0f), _rChunkSize(1.0f / chunk_size)
{
}

glm::vec3 JarVoxelLoD::get_camera_position() const
{
    return _cameraPosition;
}

bool JarVoxelLoD::process(const JarVoxelTerrain &terrain, double delta)
{
    _autoMeshCoolDown -= static_cast<float>(delta);
    if(!_automaticUpdate) return false;
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

