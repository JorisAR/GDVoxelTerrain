#ifndef LEVEL_OF_DETAIL_H
#define LEVEL_OF_DETAIL_H

#include "voxel_octree_node.h"
#include <algorithm>
#include <functional>
#include <glm/glm.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <vector>

using namespace godot;

class JarVoxelTerrain;

class JarVoxelLoD
{
  private:
    float _automaticUpdateDistance = 64;
    float _octreeScale = 1.0f;
    int _lodLevelCount = 20;
    int _shellSize = 2;
    bool _automaticUpdate = true;

    int _maxChunkSize = 10; // was UNINITIALISED and never assigned anywhere ->
                            // garbage made desired_lod() force LOD 0 on every node,
                            // subdividing the whole tree to max detail => the
                            // "infinite" build / nondeterministic crash on Linux.
    float _autoMeshCoolDown = 0.0f;
    glm::vec3 _cameraPosition = glm::vec3(0.0f);

    float lod_to_grid_size(const int lod) const;
    glm::vec3 snap_to_grid(const glm::vec3 pos, const float grid_size) const;
    bool is_in_lod_shell(int lod, glm::vec3 pos, glm::vec3 cam_pos) const;

  protected:
    
    static void _bind_methods();

  public:
    JarVoxelLoD();
    JarVoxelLoD(const bool automaticUpdate, const float automaticUpdateDistance, const int lodLevelCount, const int shellSize, const float octreeScale);

    glm::vec3 get_camera_position() const;

    bool process(const JarVoxelTerrain &terrain, double delta);
    bool update_camera_position(const JarVoxelTerrain &terrain, const bool force);

    int desired_lod(const VoxelOctreeNode &node);
    int lod_at(const glm::vec3 &position) const;

};

#endif // LEVEL_OF_DETAIL_H


