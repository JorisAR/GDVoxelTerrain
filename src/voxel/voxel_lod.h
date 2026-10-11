#ifndef LEVEL_OF_DETAIL_H
#define LEVEL_OF_DETAIL_H

#include "voxel_octree_node.h"
#include <glm/glm.hpp>
#include <vector>

class JarVoxelVolume;

// Level of detail for one volume, driven by every JarVoxelViewer in the scene.
// Viewer positions are stored in the volume's local space; a node's LOD is the
// finest LOD any viewer asks for.
//
// LOD depends only on the viewer and sample positions: around each viewer,
// nested cubes (snapped to that level's grid) of half-size
//   detail_distance + shell_size * 4 * 2^lod  chunks
// get LOD 0, 1, 2, ... Each level's ring is shell_size of its own chunks wide,
// which keeps neighbouring chunks within one LOD of each other (what the seam
// stitching requires), for any detail distance and shell size >= 1.
class JarVoxelLoD
{
  private:
    float _automaticUpdateDistance = 64;
    int _shellSize = 2;
    bool _automaticUpdate = true;
    float _chunkMeters = 16.0f;  // edge length of an LOD 0 chunk
    float _detailChunks = 0.0f;  // extra LOD 0 radius, in LOD 0 chunks

    int _maxChunkSize = 10; // nodes above this size always use LOD 0 (they are never chunks)
    std::vector<glm::vec3> _viewerPositions; // local space, as of the last (re)build

    bool is_in_lod_shell(int lod, const glm::vec3 &pos, const glm::vec3 &viewer) const;
    int lod_for_viewer(const glm::vec3 &position, const glm::vec3 &viewer) const;

  public:
    // Current viewer positions in the volume's local space (main thread).
    static std::vector<glm::vec3> gather_viewers(const JarVoxelVolume &volume);
    JarVoxelLoD() = default;
    JarVoxelLoD(bool automaticUpdate, float automaticUpdateDistance, int shellSize, float chunkMeters,
                float detailDistance);

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
