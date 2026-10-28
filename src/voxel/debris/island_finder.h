#ifndef JAR_ISLAND_FINDER_H
#define JAR_ISLAND_FINDER_H

#include "voxel_block.h"
#include <memory>
#include <vector>

// Floating pieces: solid parts of a voxel block not connected (through face
// neighbours) to its border. When the block is a region around an edit, the
// border stands for the rest of the world, so these are the parts the edit cut
// loose. Works on blocks only: a volume's region (JarVoxelVolume::sample_region)
// or a falling piece.
namespace IslandFinder
{
struct Island
{
    std::shared_ptr<VoxelBlock> block; // just this piece, with an empty margin
    glm::vec3 centroid{0.0f};          // of its solid cells, in the source block's space
    int cells = 0;                     // solid cells
};

// The floating pieces of `region` at most `maxCells` cells across on every
// axis (larger ones stay where they are).
std::vector<Island> find(const VoxelBlock &region, int maxCells);
} // namespace IslandFinder

#endif // JAR_ISLAND_FINDER_H
