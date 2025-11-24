#ifndef POPULATION_OCTREE_NODE_H
#define POPULATION_OCTREE_NODE_H

#include "octree_node.h"
#include "feature_node3d.h"
#include "sdf_modification.h"
#include <vector>


namespace godot
{

class JarWorld;
class JarVoxelTerrain;

class PopulationOctreeNode : public OctreeNode<PopulationOctreeNode> {
public:
    PopulationOctreeNode(PopulationOctreeNode* parent, const glm::vec3& center, uint32_t size)
        : OctreeNode(parent, center, size) {}

    uint32_t min_size() const override { return 5; }

    std::unique_ptr<PopulationOctreeNode> create_child_node(const glm::vec3& center, uint32_t size) override {
        return std::make_unique<PopulationOctreeNode>(this, center, size);
    }

    void update(JarWorld &world);
    void delete_features_in_bounds(JarWorld &terrain, const ModifySettings &settings);

private:
    static constexpr uint32_t FEATURE_RENDER_THRESHOLD = 4; //measured in level of details
    int _lod = 0;
    bool _isGenerated = false;
    std::vector<FeatureNode3D*> _features;

    void toggle_feature_instances(bool enable);
    void populate_leaf(JarWorld &world);
    inline bool is_minimal_leaf() const { return _sizeLog2 == min_size(); }
    bool has_surface(const float octree_scale, const float value);
};

}

#endif // POPULATION_OCTREE_NODE_H