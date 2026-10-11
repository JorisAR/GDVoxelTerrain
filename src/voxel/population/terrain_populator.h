#ifndef JAR_TERRAIN_POPULATOR_H
#define JAR_TERRAIN_POPULATOR_H

#include <algorithm>
#include <glm/glm.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <optional>

using namespace godot;

class JarTerrainPopulator : public Resource
{
    GDCLASS(JarTerrainPopulator, Resource);

  private:
    float minimum_scale = 1.0f;
    float maximum_scale = 1.0f;
    float minimum_height = 0.0f;
    float maximum_height = 0.0f;
    float minimum_slope = 0.0f;

    bool align_with_normal = false;

  public:
    JarTerrainPopulator() = default;
    virtual ~JarTerrainPopulator() = default;

    // Getters
    float get_minimum_scale() const { return minimum_scale; }
    float get_maximum_scale() const { return maximum_scale; }
    float get_minimum_height() const { return minimum_height; }
    float get_maximum_height() const { return maximum_height; }
    float get_minimum_slope() const { return minimum_slope; }
    bool get_align_with_normal() const { return align_with_normal; }

    // Setters
    void set_minimum_scale(float value) { minimum_scale = value; }
    void set_maximum_scale(float value) { maximum_scale = value; }
    void set_minimum_height(float value) { minimum_height = value; }
    void set_maximum_height(float value) { maximum_height = value; }
    void set_minimum_slope(float value) { minimum_slope = value; }
    void set_align_with_normal(bool value) { align_with_normal = value; }

    // Function to construct a Transform3D
    Transform3D build_transform(const Vector3 &origin, const Vector3 &up, float rotation, float scale) const {
        Vector3 basis_x = up.cross(Vector3(1, 0, 0)).normalized();        
        if (basis_x.length() < 0.001f) {
            basis_x = up.cross(Vector3(0, 1, 0)).normalized();
        }
        Vector3 basis_z = basis_x.cross(up).normalized();
        Basis basis(basis_x, up.normalized(), basis_z);
        basis = basis.rotated(up, rotation).scaled(Vector3(scale, scale, scale));
        return Transform3D(basis, origin);
    }

    bool is_height_in_range(const float height) const {
        return height >= minimum_height && height <= maximum_height;
    }

    bool is_slope_in_range(const float slope) const {
        return slope <= minimum_slope;
    }

  protected:
    static void _bind_methods() {
        // Binding methods for Godot
        ClassDB::bind_method(D_METHOD("get_minimum_scale"), &JarTerrainPopulator::get_minimum_scale);
        ClassDB::bind_method(D_METHOD("set_minimum_scale", "value"), &JarTerrainPopulator::set_minimum_scale);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "minimum_scale"), "set_minimum_scale", "get_minimum_scale");

        ClassDB::bind_method(D_METHOD("get_maximum_scale"), &JarTerrainPopulator::get_maximum_scale);
        ClassDB::bind_method(D_METHOD("set_maximum_scale", "value"), &JarTerrainPopulator::set_maximum_scale);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "maximum_scale"), "set_maximum_scale", "get_maximum_scale");

        ClassDB::bind_method(D_METHOD("get_minimum_height"), &JarTerrainPopulator::get_minimum_height);
        ClassDB::bind_method(D_METHOD("set_minimum_height", "value"), &JarTerrainPopulator::set_minimum_height);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "minimum_height"), "set_minimum_height", "get_minimum_height");

        ClassDB::bind_method(D_METHOD("get_maximum_height"), &JarTerrainPopulator::get_maximum_height);
        ClassDB::bind_method(D_METHOD("set_maximum_height", "value"), &JarTerrainPopulator::set_maximum_height);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "maximum_height"), "set_maximum_height", "get_maximum_height");

        ClassDB::bind_method(D_METHOD("get_minimum_slope"), &JarTerrainPopulator::get_minimum_slope);
        ClassDB::bind_method(D_METHOD("set_minimum_slope", "value"), &JarTerrainPopulator::set_minimum_slope);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "minimum_slope"), "set_minimum_slope", "get_minimum_slope");

        ClassDB::bind_method(D_METHOD("get_align_with_normal"), &JarTerrainPopulator::get_align_with_normal);
        ClassDB::bind_method(D_METHOD("set_align_with_normal", "value"), &JarTerrainPopulator::set_align_with_normal);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "align_with_normal"), "set_align_with_normal", "get_align_with_normal");

        ClassDB::bind_method(D_METHOD("build_transform", "origin", "up", "rotation", "scale"), &JarTerrainPopulator::build_transform);
    }
};

#endif // JAR_TERRAIN_POPULATOR_H
