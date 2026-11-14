#ifndef JAR_ASTEROID_BELT_H
#define JAR_ASTEROID_BELT_H

#include "body_layer.h"
#include <atomic>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/multi_mesh_instance3d.hpp>
#include <memory>
#include <vector>

using namespace godot;

// An asteroid belt around its body (usually a star): a flat ring of rocks
// between inner_radius and outer_radius from its center, thinning out over
// `thickness` above and below its plane (normal: `axis`, in the universe).
// Only the rocks near the camera exist, streamed from a hashed grid (the same
// rocks are always in the same places), in two tiers: small rocks close by,
// large ones out to hundreds of kilometers (parallax when flying through).
// Their shapes are made at startup (icospheres displaced by noise and
// craters, no assets); positions are placed from the universe in double
// precision, so nothing jitters far from the star. Lit by the scene's light.
class JarBodyAsteroids : public JarBodyLayer
{
    GDCLASS(JarBodyAsteroids, JarBodyLayer);

  public:
    static constexpr int Variants = 6;

  private:
    float _innerRadius = 1.0e8f;
    float _outerRadius = 1.1e8f;
    float _thickness = 1.5e6f;
    Vector3 _axis{0, 1, 0};
    float _density = 1.0f;
    int _seed = 1;
    Ref<Material> _material;
    Ref<Material> _defaultMaterial;

    struct Tier
    {
        float cell;    // meters
        float reach;   // meters around the camera
        float minSize; // radius range, meters
        float maxSize;
        float perCell; // rocks per cell at the belt's densest
        int detail;    // mesh subdivisions
        uint32_t salt;
    };
    static const Tier Tiers[2];
    struct Built
    {
        std::atomic<bool> done{false};
        glm::dvec3 anchor{0.0}; // universe position the instances are relative to
        std::vector<std::vector<float>> buffers; // per variant: multimesh TRANSFORM_3D rows
    };
    struct TierState
    {
        MultiMeshInstance3D *instances[Variants] = {};
        glm::dvec3 anchor{0.0};
        glm::ivec3 builtCell{INT32_MIN};
        std::shared_ptr<Built> job;
        bool hasAnchor = false;
    };
    TierState _tiers[2];
    Ref<ArrayMesh> _meshes[2][Variants];

    void start_build(int tier, const glm::dvec3 &camera, const glm::ivec3 &cell);
    Ref<Material> active_material();

  protected:
    static void _bind_methods();
    void build() override;
    void update(const JarCelestialBody &body) override;

  public:
    // Rocks per tier cell at the belt's densest, relative (1: the default).
    float get_density() const { return _density; }
    void set_density(float value) { _density = std::max(0.0f, value); }
    float get_inner_radius() const { return _innerRadius; }
    void set_inner_radius(float value) { _innerRadius = std::max(0.0f, value); }
    float get_outer_radius() const { return _outerRadius; }
    void set_outer_radius(float value) { _outerRadius = std::max(0.0f, value); }
    float get_thickness() const { return _thickness; }
    void set_thickness(float value) { _thickness = std::max(1.0f, value); }
    Vector3 get_axis() const { return _axis; }
    void set_axis(const Vector3 &value) { _axis = value; }
    int get_seed() const { return _seed; }
    void set_seed(int value) { _seed = value; }
    Ref<Material> get_material() const { return _material; }
    void set_material(const Ref<Material> &value);
    // How many rocks are shown now (both tiers).
    int get_rock_count() const;
    // The belt's density at a universe position relative to the body's
    // center, 0..1 (its profile across and through the ring).
    float profile_at(const glm::dvec3 &offset) const;
};

#endif // JAR_ASTEROID_BELT_H
