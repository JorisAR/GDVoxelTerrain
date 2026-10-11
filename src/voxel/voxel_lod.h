#ifndef LEVEL_OF_DETAIL_H
#define LEVEL_OF_DETAIL_H

#include "voxel_octree_node.h"
#include <glm/glm.hpp>
#include <vector>

class JarVoxelVolume;

// Level of detail for one volume, driven by every JarVoxelViewer in the scene.
// Viewer positions are stored in the volume's local space; a node's LOD is the
// finest LOD any viewer asks for.
class JarVoxelLoD
{
  private:
    float _automaticUpdateDistance = 64;
    float _octreeScale = 1.0f;
    int _lodLevelCount = 20;
    int _shellSize = 2;
    bool _automaticUpdate = true;

    int _maxChunkSize = 10; // nodes above this size always use LOD 0 (they are never chunks)
    std::vector<glm::vec3> _viewerPositions; // local space, as of the last (re)build

    float lod_to_grid_size(const int lod) const;
    glm::vec3 snap_to_grid(const glm::vec3 pos, const float grid_size) const;
    bool is_in_lod_shell(int lod, glm::vec3 pos, glm::vec3 cam_pos) const;
    int lod_for_viewer(const glm::vec3 &position, const glm::vec3 &viewer) const;
    // Current viewer positions in the volume's local space (main thread).
    static std::vector<glm::vec3> gather_viewers(const JarVoxelVolume &volume);

  public:
    JarVoxelLoD() = default;
    JarVoxelLoD(const bool automaticUpdate, const float automaticUpdateDistance, const int lodLevelCount,
                const int shellSize, const float octreeScale);

    // Main thread. Returns true when viewers moved far enough (or were added or
    // removed) that the octree should be rebuilt; positions are then updated.
    bool process(const JarVoxelVolume &volume);
    bool update_viewers(const JarVoxelVolume &volume, const bool force);

    // Distance from a local position to the closest viewer (inf without viewers).
    float distance_to_nearest_viewer(const glm::vec3 &position) const;

    int desired_lod(const VoxelOctreeNode &node) const;
    int lod_at(const glm::vec3 &position) const;
};

#endif // LEVEL_OF_DETAIL_H
