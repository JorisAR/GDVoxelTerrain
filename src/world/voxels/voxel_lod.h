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

namespace godot
{

  class JarVoxelTerrain;

  class JarVoxelLoD
  {
  private:
    float _automaticUpdateDistance = 64;
    float _octreeScale = 1.0f;
    float _rChunkSize = 1.0f / 16.0f;
    int _lodLevelCount = 20;
    int _shellSize = 2;
    bool _automaticUpdate = true;

    int _maxChunkSize = 4;
    float _autoMeshCoolDown;
    glm::vec3 _cameraPosition;

    inline float lod_to_grid_size(const int lod) const;
    inline glm::vec3 snap_to_grid(const glm::vec3 pos, const float grid_size) const;
    inline bool is_in_lod_shell(int lod, glm::vec3 pos, glm::vec3 cam_pos) const;

  protected:
    static void _bind_methods();

  public:
    JarVoxelLoD();
    JarVoxelLoD(const bool automaticUpdate, const float automaticUpdateDistance, const int lodLevelCount, const int shellSize, const float octreeScale, const int chunk_size);

    glm::vec3 get_camera_position() const;

    bool process(const JarVoxelTerrain &terrain, double delta);
    bool update_camera_position(const JarVoxelTerrain &terrain, const bool force);

    int desired_lod(const VoxelOctreeNode &node);
    int lod_at(const glm::vec3 &position) const;
  };
}

#endif // LEVEL_OF_DETAIL_H
