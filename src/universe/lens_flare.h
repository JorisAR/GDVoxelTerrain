#ifndef JAR_LENS_FLARE_H
#define JAR_LENS_FLARE_H

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
// in the scene; the look is on `material` (lens_flare.gdshader). Not drawn in
// the editor.
class JarLensFlare : public Node3D
{
    GDCLASS(JarLensFlare, Node3D);

  private:
    DirectionalLight3D *_light = nullptr;
    float _intensity = 1.0f;
    Ref<ShaderMaterial> _material;
    Ref<ShaderMaterial> _defaultMaterial;
    MeshInstance3D *_quad = nullptr;

    DirectionalLight3D *find_light() const;
    void update();

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
};

#endif // JAR_LENS_FLARE_H
