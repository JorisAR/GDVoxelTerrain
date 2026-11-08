#ifndef JAR_BODY_LAYER_H
#define JAR_BODY_LAYER_H

#include "atmosphere_model.h"
#include "celestial_body.h"
#include "voxel_universe.h"
#include <algorithm>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/image_texture.hpp>
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
    // The star's light at `at` above any air (sun_direction, sun_color: its
    // color above the atmosphere times its energy), for bodies far away.
    void apply_star(const Ref<ShaderMaterial> &material, const Vector3 &at);
    // Direction from a global position towards the star (a JarBodyLight's
    // body), else towards the sun light. Far bodies need their own: the light
    // is aimed at the camera.
    Vector3 star_direction_at(const Vector3 &global_position);

    // How the body is drawn for the camera (JarVoxelUniverse::get_far_view):
    // far away, smaller and nearer by `scale` about the camera (scaled space),
    // which looks the same and keeps it inside the far plane. `center`: where
    // its center is drawn.
    struct FarView : JarFarView
    {
        Vector3 center;
    };
    FarView far_view(const JarCelestialBody &body) const;
    // Far away a body is lit by its own star here (far_sun_direction,
    // far_sun_color, far_ambient), see far_light.
    void apply_far_light(const Ref<ShaderMaterial> &material, const Vector3 &at);

  public:
    // The light at `at` for something drawn in scaled space, which the scene's
    // lights do not light correctly (the star's light is aimed at the camera):
    // the direction towards the star there, its light above any air (color
    // times energy) and the starlight floor of the scene's ambient light.
    static void far_light(Node *context, const Vector3 &at, Vector3 &direction, Vector3 &color, Vector3 &ambient);
    // The body this layer belongs to: the nearest JarCelestialBody above it.
    JarCelestialBody *get_body() const;
    // The light used as the sun: `sun`, or the first DirectionalLight3D in the scene.
    DirectionalLight3D *find_sun();
    DirectionalLight3D *get_sun() const { return _sun; }
    void set_sun(DirectionalLight3D *value) { _sun = value; }
};

// Water at the body's sea level: a disc under the camera, its rings ever wider
// towards the horizon, plus a coarse sphere for what the disc does not reach
// when seen from space (its facets sag below sea level on a large planet, so
// it must not show near the ground). The shader projects both onto the exact
// sea level, so the coast is precise up close. Fog when the camera is under water. The look is set on `material`
// (water.gdshader).
class JarBodyWater : public JarBodyLayer
{
    GDCLASS(JarBodyWater, JarBodyLayer);

  private:
    float _levelOffset = 0.0f;
    float _discRadius = 0.0f;
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
    Ref<ShaderMaterial> get_material() const { return _material; }
    void set_material(const Ref<ShaderMaterial> &value);
    Ref<ShaderMaterial> get_underwater_material() const { return _underwaterMaterial; }
    void set_underwater_material(const Ref<ShaderMaterial> &value);
};

// A physically based atmosphere around a round body (AtmosphereModel, after
// Hillaire 2020): Rayleigh and Mie scattering, ozone, multiple scattering. Blue
// sky, orange and red sunsets, deep blue twilight, a glowing rim from space,
// distant terrain fading to blue. Its thickness is a fraction of the sea level
// radius; the air's properties are set here, the look on `material`
// (atmosphere.gdshader; atmosphere_extinction.gdshader dims what is behind).
//
// It also lights the scene while the camera is inside it: stars
// (JarBodyLight) shine through it (their light reddens towards the horizon),
// and the scene's ambient light becomes the sky's light, on top of the
// Environment's own ambient light (starlight at night).
class JarBodyAtmosphere : public JarBodyLayer
{
    GDCLASS(JarBodyAtmosphere, JarBodyLayer);

  private:
    float _thickness = 0.5f;
    Vector3 _rayleighDepth{0.02f, 0.045f, 0.11f};
    float _rayleighHeight = 0.2f;
    float _mieDepth = 0.006f;
    float _mieHeight = 0.075f;
    float _mieAnisotropy = 0.7f;
    Vector3 _ozoneDepth{0.0042f, 0.0117f, 0.0005f};
    float _ozoneHeight = 0.175f;
    float _ozoneWidth = 0.15f;
    Color _groundAlbedo{0.3f, 0.3f, 0.3f};
    float _ambientStrength = 1.0f;
    float _sunsetStrength = 5.0f;

    Ref<ShaderMaterial> _material;
    Ref<ShaderMaterial> _defaultMaterial;
    Ref<ShaderMaterial> _extinction; // first pass, dims what is behind
    MeshInstance3D *_shell = nullptr;
    float _shellRadius = -1.0f;
    AtmosphereModel _model;
    Ref<ImageTexture> _transmittanceLut, _multiScatteringLut;
    bool _ownsAmbient = false;

    AtmosphereParams params_for(const JarCelestialBody::Frame &frame) const;
    // In scaled space (`view`): centered on its drawn center, every length
    // times the scale and every per-meter coefficient divided by it, so the
    // same shader draws the same picture.
    void apply_air(const Ref<ShaderMaterial> &material, const FarView &view) const;
    void apply_ambient(const JarCelestialBody &body);

  protected:
    static void _bind_methods();
    void _notification(int what);
    void build() override;
    void update(const JarCelestialBody &body) override;

  public:
    static constexpr const char *Group = "jar_atmosphere";

    Ref<ShaderMaterial> get_active_material();
    // The model as last built (empty until the body is known).
    const AtmosphereModel &get_model() const { return _model; }
    // Sunlight left at a global position for a star in `direction` (1 above
    // the atmosphere; 0 behind the planet).
    Color get_sunlight_at(const Vector3 &global_position, const Vector3 &direction) const;
    // Sunlight at sea level with the star overhead (lights are balanced to it:
    // a high sun keeps its light's color).
    Color get_noon_sunlight() const;
    // Whether a global position is inside the atmosphere.
    bool contains(const Vector3 &global_position) const;

    float get_thickness() const { return _thickness; }
    void set_thickness(float value) { _thickness = std::clamp(value, 0.01f, 1.0f); }
    Vector3 get_rayleigh_depth() const { return _rayleighDepth; }
    void set_rayleigh_depth(const Vector3 &value) { _rayleighDepth = value.max(Vector3()); }
    float get_rayleigh_height() const { return _rayleighHeight; }
    void set_rayleigh_height(float value) { _rayleighHeight = std::clamp(value, 0.01f, 2.0f); }
    float get_mie_depth() const { return _mieDepth; }
    void set_mie_depth(float value) { _mieDepth = std::max(0.0f, value); }
    float get_mie_height() const { return _mieHeight; }
    void set_mie_height(float value) { _mieHeight = std::clamp(value, 0.01f, 2.0f); }
    float get_mie_anisotropy() const { return _mieAnisotropy; }
    void set_mie_anisotropy(float value) { _mieAnisotropy = std::clamp(value, 0.0f, 0.99f); }
    Vector3 get_ozone_depth() const { return _ozoneDepth; }
    void set_ozone_depth(const Vector3 &value) { _ozoneDepth = value.max(Vector3()); }
    float get_ozone_height() const { return _ozoneHeight; }
    void set_ozone_height(float value) { _ozoneHeight = std::clamp(value, 0.0f, 1.0f); }
    float get_ozone_width() const { return _ozoneWidth; }
    void set_ozone_width(float value) { _ozoneWidth = std::clamp(value, 0.01f, 1.0f); }
    Color get_ground_albedo() const { return _groundAlbedo; }
    void set_ground_albedo(const Color &value) { _groundAlbedo = value; }
    float get_sunset_strength() const { return _sunsetStrength; }
    void set_sunset_strength(float value) { _sunsetStrength = std::max(0.0f, value); }
    float get_ambient_strength() const { return _ambientStrength; }
    void set_ambient_strength(float value) { _ambientStrength = std::max(0.0f, value); }
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
// in the scene) from the body's center at the camera (each time it moved a
// little: re-aiming shadows every frame makes their edges crawl), and tells a
// shader sky its real angular size (jar_sun_size). Near a surface, shadows
// fade out as it reaches the horizon. With `physical_size` the light gets that
// size too: soft shadows (PCSS), which need more shadow bias. Color and energy
// stay on the light. The light then
// moves with the star as the universe turns (day and night, seasons), and the
// other layers (water, atmosphere, clouds) use it as their sun.
//
// With `reflects` set (another JarBodyLight: the sun) it is a moon instead:
// its light (`sun` must be set: its own DirectionalLight3D) comes from the
// body, as strong as `energy` at full moon and following the phase, and
// draws no disk in the sky (the moon itself is drawn).
class JarBodyLight : public JarBodyLayer
{
    GDCLASS(JarBodyLight, JarBodyLayer);

  private:
    bool _physicalSize = false;
    JarBodyLight *_reflects = nullptr;
    float _energy = 0.08f;
    float _spaceMinSensitivity = 0.0f;

  protected:
    static void _bind_methods();
    void update(const JarCelestialBody &body) override;
    void apply_space_exposure(Camera3D *camera, const Vector3 &at, const Vector3 &towards);

  public:
    // Lights driven by a JarBodyLight are in this group (JarVoxelUniverse
    // leaves them to it); a star's are also in StarGroup (other layers use
    // them as their sun), a moon's in MoonGroup.
    static constexpr const char *LightGroup = "jar_body_light";
    static constexpr const char *StarGroup = "jar_star_light";
    static constexpr const char *MoonGroup = "jar_moon_light";

    // Unit direction from `global_position` towards the star.
    Vector3 get_direction_at(const Vector3 &global_position) const;
    // The star's apparent diameter seen from `global_position`, in degrees.
    float get_angular_diameter_at(const Vector3 &global_position) const;

    // Of a moon: the part of full moon light it gives the camera (0 new moon,
    // 1 full moon), from where the sun is.
    float get_phase_at(const Vector3 &global_position) const;

    bool get_physical_size() const { return _physicalSize; }
    void set_physical_size(bool value) { _physicalSize = value; }
    JarBodyLight *get_reflects() const { return _reflects; }
    void set_reflects(JarBodyLight *value) { _reflects = value; }
    float get_energy() const { return _energy; }
    void set_energy(float value) { _energy = std::max(0.0f, value); }
    float get_space_min_sensitivity() const { return _spaceMinSensitivity; }
    void set_space_min_sensitivity(float value) { _spaceMinSensitivity = std::max(0.0f, value); }
};

#endif // JAR_BODY_LAYER_H
