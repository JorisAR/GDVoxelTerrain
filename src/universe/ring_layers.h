#ifndef JAR_RING_LAYERS_H
#define JAR_RING_LAYERS_H

#include "body_layer.h"
#include <godot_cpp/classes/gradient.hpp>
#include <godot_cpp/classes/image_texture.hpp>

using namespace godot;

// Rings around a body, in the plane across its spin axis: a flat annulus
// from `inner_radius` to `outer_radius` (multiples of the body's radius). How
// dense and what color they are along the radius is a profile made once (a
// texture of a few kilobytes, in about a millisecond): Saturn's by default (the
// faint C ring, the dense B ring, the Cassini division, the A ring with the
// Encke gap, the narrow F ring), or `profile` (a Gradient: color, and opacity
// in alpha), with fine ringlets on top. Lit by the star: brighter from the
// lit side, glowing when the sun is behind them (forward scattering), opaque
// seen edge-on, and in the planet's shadow. A JarBodyGasGiant on the same body
// shows their shadow. Like every layer, drawn in scaled space beyond the
// camera's far plane, so they are seen from any distance. The look is set on
// `material` (rings.gdshader).
class JarBodyRings : public JarBodyLayer
{
    GDCLASS(JarBodyRings, JarBodyLayer);

  private:
    float _innerRadius = 1.24f;
    float _outerRadius = 2.33f;
    Ref<Gradient> _profile;
    float _ringlets = 0.6f;
    float _opacity = 1.0f;
    int _seed = 1;
    Ref<ShaderMaterial> _material;
    Ref<ShaderMaterial> _defaultMaterial;
    MeshInstance3D *_shell = nullptr;
    float _shellRadius = -1.0f; // the mesh's radii are stale below 0
    Ref<ImageTexture> _texture;
    bool _dirty = true;

    void changed();

  protected:
    static void _bind_methods();
    void build() override;
    void update(const JarCelestialBody &body) override;

  public:
    Ref<ShaderMaterial> get_active_material();
    // The profile as drawn: color in rgb, opacity (seen face-on) in alpha,
    // from the inner to the outer edge.
    Ref<ImageTexture> get_profile_texture();
    // Opacity seen face-on at a distance from the body's center, in multiples
    // of its radius.
    float get_opacity_at(float radius);
    // The ring plane's normal (global).
    Vector3 get_normal(const JarCelestialBody &body) const;

    float get_inner_radius() const { return _innerRadius; }
    void set_inner_radius(float value);
    float get_outer_radius() const { return _outerRadius; }
    void set_outer_radius(float value);
    Ref<Gradient> get_profile() const { return _profile; }
    void set_profile(const Ref<Gradient> &value);
    float get_ringlets() const { return _ringlets; }
    void set_ringlets(float value);
    float get_opacity() const { return _opacity; }
    void set_opacity(float value);
    int get_seed() const { return _seed; }
    void set_seed(int value);
    Ref<ShaderMaterial> get_material() const { return _material; }
    void set_material(const Ref<ShaderMaterial> &value);
};

// A gas giant's visible surface: a sphere at the body's sea level with
// latitude bands, turbulence, storms and limb darkening, from noise in the
// shader (nothing baked). Shows the shadow of a JarBodyRings on the same body.
// The look is set on `material` (gas_giant.gdshader).
class JarBodyGasGiant : public JarBodyLayer
{
    GDCLASS(JarBodyGasGiant, JarBodyLayer);

  private:
    Ref<ShaderMaterial> _material;
    Ref<ShaderMaterial> _defaultMaterial;
    MeshInstance3D *_shell = nullptr;
    uint64_t _rings = 0; // instance id of the rings found on the body
    float _flattening = 0.0f;

  protected:
    static void _bind_methods();
    void build() override;
    void update(const JarCelestialBody &body) override;

  public:
    Ref<ShaderMaterial> get_active_material();

    Ref<ShaderMaterial> get_material() const { return _material; }
    void set_material(const Ref<ShaderMaterial> &value);
    // How much shorter the polar radius is than the equator's (fast spinning
    // giants bulge: Saturn 0.1, Jupiter 0.065).
    float get_flattening() const { return _flattening; }
    void set_flattening(float value) { _flattening = std::clamp(value, 0.0f, 0.5f); }
};

#endif // JAR_RING_LAYERS_H
