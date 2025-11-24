#ifndef JAR_TERRAIN_FEATURE_H
#define JAR_TERRAIN_FEATURE_H

#include <algorithm>
#include <glm/glm.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <optional>
#include "terrain_populator.h"
#include <set>

namespace godot {

class JarTerrainFeature : public JarTerrainPopulator
{
    GDCLASS(JarTerrainFeature, JarTerrainPopulator);

  private:
    Ref<PackedScene> feature_scene;
    float weight = 1.0f;
    int max_lod = 0;

  public:
    virtual ~JarTerrainFeature() = default;

    Ref<PackedScene> get_feature_scene() const { return feature_scene; }
    float get_weight() const { return weight; }
    int get_max_lod() const { return max_lod; }


    void set_feature_scene(const Ref<PackedScene> &value) { feature_scene = value; }
    void set_weight(float value) { weight = value; }
    void set_max_lod(int value) { max_lod = value; }

  protected:
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("get_feature_scene"), &JarTerrainFeature::get_feature_scene);
        ClassDB::bind_method(D_METHOD("set_feature_scene", "value"), &JarTerrainFeature::set_feature_scene);
        ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "feature_scene", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_feature_scene", "get_feature_scene");


        ClassDB::bind_method(D_METHOD("get_weight"), &JarTerrainFeature::get_weight);
        ClassDB::bind_method(D_METHOD("set_weight", "value"), &JarTerrainFeature::set_weight);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "weight"), "set_weight", "get_weight");

        ClassDB::bind_method(D_METHOD("get_max_lod"), &JarTerrainFeature::get_max_lod);
        ClassDB::bind_method(D_METHOD("set_max_lod", "value"), &JarTerrainFeature::set_max_lod);
        ADD_PROPERTY(PropertyInfo(Variant::INT, "max_lod"), "set_max_lod", "get_max_lod");
    }
};

} // namespace godot

#endif // JAR_TERRAIN_FEATURE_H
