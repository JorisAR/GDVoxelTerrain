#ifndef JAR_LENS_FLARE_H
#define JAR_LENS_FLARE_H

#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/shader_material.hpp>

using namespace godot;

// A lens flare for the sun: glow, starburst, a halo ring, ghosts along the
// line through the screen's center and a horizontal streak, drawn over the
// whole screen after everything else. It follows `light` (else a
// JarBodyLight star's light, else the first DirectionalLight3D) and the
// current camera, takes the light's color (reddened at sunset by a
// JarBodyAtmosphere, gone below the horizon), and hides as terrain or a moon
// covers the sun (measured from the depth buffer around it). Put one anywhere
// in the scene; the look is in its properties, which also apply to a `material`
// of your own. Shown in the editor as well, while `editor_preview` is on.

// Name, default and inspector range of the look's numbers. Each is a property
// and a uniform of lens_flare.gdshader of that name.
#define JAR_FLARE_FLOATS(X)                                                                                            \
    X(glow_strength, 0.35f, "0,2,0.01,or_greater")                                                                     \
    X(glow_size, 0.12f, "0.001,1,0.001,or_greater")                                                                    \
    X(starburst_strength, 0.2f, "0,2,0.01,or_greater")                                                                 \
    X(starburst_size, 0.2f, "0.001,1,0.001,or_greater")                                                                \
    X(starburst_sharpness, 24.0f, "1,128,0.5")                                                                         \
    X(starburst_rotation, 0.0f, "-180,180,0.1,radians_as_degrees")                                                     \
    X(streak_strength, 0.05f, "0,2,0.01,or_greater")                                                                   \
    X(streak_width, 0.0045f, "0.0005,0.05,0.0001")                                                                     \
    X(streak_length, 0.33f, "0.01,2,0.01,or_greater")                                                                  \
    X(halo_strength, 0.02f, "0,1,0.001,or_greater")                                                                    \
    X(halo_radius, 0.42f, "0.05,1,0.01,or_greater")                                                                    \
    X(halo_width, 0.03f, "0.002,0.2,0.001")                                                                            \
    X(halo_chromatic, 0.04f, "0,0.2,0.001")                                                                            \
    X(halo_arc, 0.7f, "0,1,0.01")                                                                                      \
    X(ghost_strength, 0.06f, "0,1,0.001,or_greater")                                                                   \
    X(ghost_softness, 0.3f, "0,1,0.01")                                                                                \
    X(ghost_fill, 0.25f, "0,1,0.01")                                                                                   \
    X(ghost_chromatic, 0.03f, "0,0.2,0.001")                                                                           \
    X(ghost_rotation, 0.0f, "-180,180,0.1,radians_as_degrees")                                                         \
    X(occlusion_size, 2.0f, "0.5,8,0.01")

class JarLensFlare : public Node3D
{
    GDCLASS(JarLensFlare, Node3D);

  private:
    DirectionalLight3D *_light = nullptr;
    float _intensity = 1.0f;
    bool _editorPreview = true;
#define JAR_FLARE_MEMBER(name, value, range) float _##name = value;
    JAR_FLARE_FLOATS(JAR_FLARE_MEMBER)
#undef JAR_FLARE_MEMBER
    int _starburstRays = 6;
    Color _streakTint = Color(1, 1, 1);
    int _ghostCount = 6;
    int _ghostSeed = 1;
    int _ghostSides = 6;
    float _ghostSize = 0.05f;      // screen heights
    float _ghostSpread = 1.0f;     // how far along the line through the center
    float _ghostSaturation = 0.3f; // of their random colors
    bool _lookDirty = true;
    Ref<ShaderMaterial> _material;
    Ref<ShaderMaterial> _defaultMaterial;
    MeshInstance3D *_quad = nullptr;

    DirectionalLight3D *find_light() const;
    void update();
    Camera3D *current_camera() const;
    void make_quad();
    void apply_look(); // the look's uniforms, on the material in use

  protected:
    static void _bind_methods();
    void _notification(int what);

  public:
    Ref<ShaderMaterial> get_active_material();

    DirectionalLight3D *get_light() const { return _light; }
    void set_light(DirectionalLight3D *value) { _light = value; }
    float get_intensity() const { return _intensity; }
    void set_intensity(float value) { _intensity = value < 0.0f ? 0.0f : value; }
    Ref<ShaderMaterial> get_material() const { return _material; }
    void set_material(const Ref<ShaderMaterial> &value);
    bool get_editor_preview() const { return _editorPreview; }
    void set_editor_preview(bool value);

#define JAR_FLARE_ACCESS(name, value, range)                                                                           \
    float get_##name() const { return _##name; }                                                                       \
    void set_##name(float v)                                                                                           \
    {                                                                                                                  \
        _##name = v;                                                                                                   \
        _lookDirty = true;                                                                                             \
    }
    JAR_FLARE_FLOATS(JAR_FLARE_ACCESS)
#undef JAR_FLARE_ACCESS
    int get_starburst_rays() const { return _starburstRays; }
    void set_starburst_rays(int value);
    Color get_streak_tint() const { return _streakTint; }
    void set_streak_tint(const Color &value);
    int get_ghost_count() const { return _ghostCount; }
    void set_ghost_count(int value);
    int get_ghost_seed() const { return _ghostSeed; }
    void set_ghost_seed(int value);
    int get_ghost_sides() const { return _ghostSides; }
    void set_ghost_sides(int value);
    float get_ghost_size() const { return _ghostSize; }
    void set_ghost_size(float value);
    float get_ghost_spread() const { return _ghostSpread; }
    void set_ghost_spread(float value);
    float get_ghost_saturation() const { return _ghostSaturation; }
    void set_ghost_saturation(float value);
};

#endif // JAR_LENS_FLARE_H
