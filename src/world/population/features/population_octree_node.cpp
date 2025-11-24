#include "population_octree_node.h"
#include "alias_table.h"
#include "poisson_disc_sampler.h"
#include "alias_table.h"
#include "utils.h"
#include "world.h"
#include "voxel_terrain.h"
#include "feature_node3d.h"

void godot::PopulationOctreeNode::update(JarWorld &world)
{
    auto terrain = world.get_terrain();
    if (terrain == nullptr || terrain->get_sdf() == nullptr)
        return;

    _lod = terrain->desired_lod(*this);

    if (is_minimal_leaf())
    {
        if (_lod <= FEATURE_RENDER_THRESHOLD)
        {
            if (!_isGenerated)
            {
                populate_leaf(world);
                _isGenerated = true;
            }
            else
            {
                toggle_feature_instances(true);
            }
        }
        else
        {
            toggle_feature_instances(false);
        }

        return;
    }

    if (_lod < 0)
        return;

    _lod -= min_size();

    if (!_isGenerated)
    {
        float value = terrain->get_sdf()->distance(_center);
        if (has_surface(terrain->get_octree_scale(), value) && (_sizeLog2 > _lod))
        {
            subdivide(terrain->get_octree_scale());
            _isGenerated = true;
        }
        // if we don't subdivide further, we mark it as a fully realized subtree
        if (is_leaf() && (_sizeLog2 > _lod || _sizeLog2 == min_size()))
        { //
            _isGenerated = true;
            return;
        }
    }

    if (!is_leaf())
        for (auto &child : *_children)
            child->update(world);
}

void godot::PopulationOctreeNode::delete_features_in_bounds(JarWorld &world, const ModifySettings &settings)
{
    auto terrain = world.get_terrain();
    if (terrain == nullptr)
        return;
    if (settings.sdf.is_null())
    {
        UtilityFunctions::print("sdf settings invalid");
        return;
    }

    auto bounds = get_bounds(terrain->get_octree_scale());
    if (!settings.bounds.intersects(bounds))
        return;

    if (!_isGenerated)
        return;
       
    if(is_minimal_leaf())
    {
        for (auto it = _features.begin(); it != _features.end();)
        {
            auto feature = *it;
            if (settings.bounds.contains_point(Utils::to_vec3(feature->get_position())))
            {
                feature->queue_free();
                it = _features.erase(it);
            }
            else
            {
                ++it;
            }
        }
        return;
    }

    if (!is_leaf())
        for (auto &child : *_children)
            child->delete_features_in_bounds(world, settings);
}

void godot::PopulationOctreeNode::toggle_feature_instances(bool enable)
{
    for (auto feature : _features)
    {
        if(feature)
        feature->toggle(enable);
    }    
}

void godot::PopulationOctreeNode::populate_leaf(JarWorld &world)
{
    auto terrain = world.get_terrain();
    Bounds bounds = get_bounds(terrain->get_octree_scale());
    Vector3 center = Utils::to_vector3(get_center());
    glm::vec3 gravity = glm::normalize(Utils::to_vec3(world.get_gravity_vector(center)));

    glm::vec3 up = -gravity;
    glm::vec3 right = glm::normalize(glm::cross(up, glm::vec3(0,1,0)));
    if (glm::length(right) < 1e-4f) // handle colinearity
        right = glm::normalize(glm::cross(up, glm::vec3(1,0,0)));
    glm::vec3 forward = glm::normalize(glm::cross(right, up));

    glm::vec3 halfExtents = (bounds.max_corner - bounds.min_corner) * 0.5f;
    float width  = glm::dot(bounds.max_corner - bounds.min_corner, right);
    float height = glm::dot(bounds.max_corner - bounds.min_corner, forward);


    float minSpacing = 2.0f;
    auto samples2D = PoissonDiscSampler::generate(width, height, minSpacing);

    std::vector<glm::vec3> samplePositions;
    samplePositions.reserve(samples2D.size());
    for (auto &p : samples2D) {
        glm::vec3 pos = get_center()
                      + (p.x - width * 0.5f) * right
                      + (p.y - height * 0.5f) * forward;
        samplePositions.push_back(pos);
    }

    auto features = terrain->get_terrain_features();
    std::vector<double> weights;
    weights.reserve(features.size());
    for (int i = 0; i < features.size(); ++i){
        Ref<JarTerrainFeature> feature = features[i];
        weights.push_back(feature->get_weight());
    }
        
    AliasTable alias_table(weights);

    for (auto &pos : samplePositions) {
        glm::vec3 from = pos + up * 10.0f; 
        glm::vec3 dir  = -up;
        auto result = terrain->get_sdf()->ray_march(from, dir);
        if(!result.has_value()) continue;
        auto ground_pos = result.value();        

        int feature_index = alias_table.sample(UtilityFunctions::randf(), UtilityFunctions::randf());
        Ref<JarTerrainFeature> feature = features[feature_index];
        if (feature.is_null())
            continue;

        float angle = UtilityFunctions::randf() * Math_PI * 2.0f;
        Quaternion rotation = Quaternion(Utils::to_vector3(up), angle);

        float scale = UtilityFunctions::randf_range(feature->get_minimum_scale(), feature->get_maximum_scale());
        
        
        Node3D *instance = static_cast<Node3D *>(feature->get_feature_scene()->instantiate());
        if (instance == nullptr)
            continue;
        world.add_child(instance);
        instance->set_position(Utils::to_vector3(ground_pos));
        
        FeatureNode3D* feature_instance = static_cast<FeatureNode3D*>(instance);
        if (feature_instance != nullptr)
            _features.push_back(feature_instance);
    }
}


bool godot::PopulationOctreeNode::has_surface(const float octree_scale, const float value)
{
    //(3*(1/2)^3)^(1/3) = 1.44224957 for d instead of r
    return std::abs(value) < (1 << _sizeLog2) * octree_scale * 1.44224957f * 1.75f;
}