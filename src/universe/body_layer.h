#ifndef JAR_BODY_LAYER_H
#define JAR_BODY_LAYER_H

#include "celestial_body.h"
#include <algorithm>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>

using namespace godot;

// Something drawn around a JarCelestialBody from its frame (center, up, sea
// level): water, atmosphere, clouds. Put it under the body (any depth); it
// updates every frame, in the editor too. Without a body it does nothing.
class JarBodyLayer : public Node3D
{
    GDCLASS(JarBodyLayer, Node3D);

  private:
    uint64_t _foundSun = 0; // instance id of the light found last

  protected:
    DirectionalLight3D *_sun = nullptr;

    static void _bind_methods();
    void _notification(int what);
    // Every frame, with the body (never null).
    virtual void update(const JarCelestialBody &body)
    {
    }
    // Once, entering the tree: create the internal nodes.
    virtual void build()
    {
    }
    // An internal mesh following nothing (placed in global space each frame).
    MeshInstance3D *add_shell();
    // A material with the given shader, drawn after `priority` lower ones.
    static Ref<ShaderMaterial> make_material(const String &shaderPath, int priority);
    // Where the camera is (the body's center far above the sea without one).
    Vector3 eye(const JarCelestialBody::Frame &frame) const;
    // Sun direction and color to `material` (sun_direction, sun_color).
    void apply_sun(const Ref<ShaderMaterial> &material);

  public:
    // The body this layer belongs to: the nearest JarCelestialBody above it.
    JarCelestialBody *get_body() const;
    // The light used as the sun: `sun`, or the first DirectionalLight3D in the scene.
    DirectionalLight3D *find_sun();
    DirectionalLight3D *get_sun() const { return _sun; }
    void set_sun(DirectionalLight3D *value) { _sun = value; }
};

// Water at the body's sea level: a sphere (or a plane on a flat body), drawn as
// a coarse mesh for distant water plus a dense patch that follows the camera;
// the shader projects both onto the exact sea level, so the coast is precise up
// close. Fog when the camera is under water. The look is set on `material`
// (water.gdshader).
class JarBodyWater : public JarBodyLayer
{
    GDCLASS(JarBodyWater, JarBodyLayer);

  private:
    float _levelOffset = 0.0f;
    float _patchSize = 1200.0f;
    int _patchResolution = 200;
    Ref<ShaderMaterial> _material;
    Ref<ShaderMaterial> _underwaterMaterial;
    Ref<ShaderMaterial> _defaultMaterial;
    Ref<ShaderMaterial> _defaultUnderwater;
    MeshInstance3D *_far = nullptr;
    MeshInstance3D *_near = nullptr;
    MeshInstance3D *_fog = nullptr;
    float _sphereRadius = -1.0f; // of the far mesh, when a sphere

    void apply_materials();
    void set_hole(const Vector3 &at, float radius);

  protected:
    static void _bind_methods();
    void build() override;
    void update(const JarCelestialBody &body) override;

  public:
    Ref<ShaderMaterial> get_active_material();
    Ref<ShaderMaterial> get_active_underwater_material();

    float get_level_offset() const { return _levelOffset; }
    void set_level_offset(float value) { _levelOffset = value; }
    float get_patch_size() const { return _patchSize; }
    void set_patch_size(float value);
    int get_patch_resolution() const { return _patchResolution; }
    void set_patch_resolution(int value);
    Ref<ShaderMaterial> get_material() const { return _material; }
    void set_material(const Ref<ShaderMaterial> &value);
    Ref<ShaderMaterial> get_underwater_material() const { return _underwaterMaterial; }
    void set_underwater_material(const Ref<ShaderMaterial> &value);
};

// A physically based atmosphere (Rayleigh and Mie single scattering) around a
// round body: blue sky and red sunsets from the ground, a glowing rim from
// space, haze over distant terrain. Its thickness is a fraction of the sea
// level radius. The look is set on `material` (atmosphere.gdshader).
class JarBodyAtmosphere : public JarBodyLayer
{
    GDCLASS(JarBodyAtmosphere, JarBodyLayer);

  private:
    float _thickness = 0.25f;
    Ref<ShaderMaterial> _material;
    Ref<ShaderMaterial> _defaultMaterial;
    MeshInstance3D *_shell = nullptr;
    float _shellRadius = -1.0f;

  protected:
    static void _bind_methods();
    void build() override;
    void update(const JarCelestialBody &body) override;

  public:
    Ref<ShaderMaterial> get_active_material();

    float get_thickness() const { return _thickness; }
    void set_thickness(float value) { _thickness = std::clamp(value, 0.01f, 1.0f); }
    Ref<ShaderMaterial> get_material() const { return _material; }
    void set_material(const Ref<ShaderMaterial> &value);
};

// A basic cloud layer: a shell `altitude` meters above the body's sea level (a
// sphere, or a plane on a flat body). Shape, wind and lighting are set on
// `material` (clouds.gdshader).
class JarBodyClouds : public JarBodyLayer
{
    GDCLASS(JarBodyClouds, JarBodyLayer);

  private:
    float _altitude = 320.0f;
    Ref<ShaderMaterial> _material;
    Ref<ShaderMaterial> _defaultMaterial;
    MeshInstance3D *_shell = nullptr;
    float _shellRadius = -1.0f; // when a sphere; 0 when a plane

  protected:
    static void _bind_methods();
    void build() override;
    void update(const JarCelestialBody &body) override;

  public:
    Ref<ShaderMaterial> get_active_material();

    float get_altitude() const { return _altitude; }
    void set_altitude(float value) { _altitude = value; }
    Ref<ShaderMaterial> get_material() const { return _material; }
    void set_material(const Ref<ShaderMaterial> &value);
};

// Makes its body a star: aims a DirectionalLight3D (`sun`, else the first one
// in the scene) from the body's center at the camera every frame, and gives it
// the body's real angular size (the sky's sun disk, LIGHT0_SIZE, and the
// softness of shadows). Color and energy stay on the light. The light then
// moves with the star as the universe turns (day and night, seasons), and the
// other layers (water, atmosphere, clouds) use it as their sun.
class JarBodyLight : public JarBodyLayer
{
    GDCLASS(JarBodyLight, JarBodyLayer);

  private:
    bool _physicalSize = true;

  protected:
    static void _bind_methods();
    void update(const JarCelestialBody &body) override;

  public:
    // Lights driven by a star are in this group (JarVoxelUniverse leaves them
    // to it; other layers prefer them as their sun).
    static constexpr const char *LightGroup = "jar_body_light";

    // Unit direction from `global_position` towards the star.
    Vector3 get_direction_at(const Vector3 &global_position) const;
    // The star's apparent diameter seen from `global_position`, in degrees.
    float get_angular_diameter_at(const Vector3 &global_position) const;

    bool get_physical_size() const { return _physicalSize; }
    void set_physical_size(bool value) { _physicalSize = value; }
};

#endif // JAR_BODY_LAYER_H
