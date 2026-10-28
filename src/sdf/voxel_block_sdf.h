#ifndef JAR_VOXEL_BLOCK_SDF_H
#define JAR_VOXEL_BLOCK_SDF_H

#include "modify_settings.h"
#include "signed_distance_field.h"
#include "voxel_block.h"
#include <memory>
#include <vector>

// The distance field of a voxel block (trilinear between its cells; beyond
// it, the distance to it), in the block's own coordinates. Edits with it copy
// a piece of terrain somewhere: subtracting a piece cut out of a volume from
// that volume removes exactly it, adding it back elsewhere places it there.
class JarVoxelBlockSdf : public JarSignedDistanceField
{
    GDCLASS(JarVoxelBlockSdf, JarSignedDistanceField);

  private:
    std::shared_ptr<const VoxelBlock> _block;

  public:
    void set_block(const std::shared_ptr<const VoxelBlock> &block)
    {
        _block = block;
        emit_changed();
    }
    const std::shared_ptr<const VoxelBlock> &get_block() const { return _block; }

    float distance(const glm::vec3 &pos) const override
    {
        return _block ? _block->sample(pos) : 1e9f;
    }

    Bounds bounds() const override
    {
        if (!_block)
            return Bounds();
        return _block->bounds().expanded(_block->spacing);
    }

  protected:
    static void _bind_methods()
    {
    }
};

// The block's own materials for an edit with JarVoxelBlockSdf (each changed
// node takes its nearest cell's), with material indices mapped from the
// block's volume to the edited one (`remap[i]` for index i; empty: same).
class VoxelBlockMaterials : public EditMaterials
{
  private:
    std::shared_ptr<const VoxelBlock> _block;
    std::vector<int> _remap;

  public:
    VoxelBlockMaterials(std::shared_ptr<const VoxelBlock> block, std::vector<int> remap = {})
        : _block(std::move(block)), _remap(std::move(remap))
    {
    }

    MaterialWeights at(const glm::vec3 &sdfPoint) const override
    {
        MaterialWeights m = _block->materials_at(_block->nearest_cell(sdfPoint));
        if (!_remap.empty())
            for (int i = 0; i < MaterialWeights::Slots; ++i)
                if (m.weights[i] > 0 && m.ids[i] < _remap.size())
                    m.ids[i] = uint8_t(std::clamp(_remap[m.ids[i]], 0, MaterialWeights::MaxMaterial));
        return m;
    }
};

#endif // JAR_VOXEL_BLOCK_SDF_H
