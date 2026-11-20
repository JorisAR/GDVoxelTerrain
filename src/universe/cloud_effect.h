#ifndef JAR_CLOUD_EFFECT_H
#define JAR_CLOUD_EFFECT_H

#include <cstdint>
#include <godot_cpp/classes/compositor_effect.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/render_data.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/projection.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <algorithm>

using namespace godot;

// The values clouds_trace.glsl and clouds_resolve.glsl read (std140: only
// vec4 and mat4, column-major). JarBodyClouds fills the layer's part every
// frame, JarCloudsEffect the camera's.
struct alignas(16) JarCloudParams
{
    float inv_projection[16];
    float camera[16];
    float world_to_body[16];
    float world_to_weather[16];
    float prev_view_projection[16];
    float prev_body_to_world[16];
    float center_scale[4];
    float prev_center_scale[4];
    float layer[4];
    float up_radial[4];
    float pole[4];
    float sun_direction[4];
    float sun_color[4];
    float moon_direction[4];
    float moon_color[4];
    float ambient_light[4];
    float cloud_color[4];
    float sky_light[4];
    float shape[4];
    float shape2[4];
    float light[4];
    float shape_offset[4];
    float detail_offset[4];
    float flat_offset[4];
    float quality[4];
    int32_t sizes[4];
    int32_t rect[4];
    int32_t frame[4];
};

// Values into JarCloudParams fields (mat4 column-major, vec4).
namespace JarCloudPack
{
inline void put(float *out, const Basis &b)
{
    // A mat4 (column-major) with the basis in its upper left.
    std::fill(out, out + 16, 0.0f);
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            out[c * 4 + r] = float(b.rows[r][c]);
    out[15] = 1.0f;
}

inline void put(float *out, const Transform3D &t)
{
    put(out, t.basis);
    out[12] = float(t.origin.x);
    out[13] = float(t.origin.y);
    out[14] = float(t.origin.z);
}

inline void put(float *out, const Projection &p)
{
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            out[c * 4 + r] = float(p.columns[c][r]);
}

inline void put4(float *out, const Vector3 &v, float w)
{
    out[0] = float(v.x);
    out[1] = float(v.y);
    out[2] = float(v.z);
    out[3] = w;
}
} // namespace JarCloudPack

// Draws the clouds of every JarBodyClouds (volumetric, ray marched) before
// the transparent pass: traced at half resolution, one pixel of each 2x2
// block per frame, and accumulated over frames through the body's own space
// (so it holds still as the planet turns and the camera moves). The result
// goes to two textures per layer that its shell's material puts on screen at
// full resolution, in order with the water and the atmosphere.
//
// Put one in the Compositor of the scene's WorldEnvironment (or camera);
// JarBodyClouds adds one there if there is none. All of them share the same
// work: a second one does nothing.
class JarCloudsEffect : public CompositorEffect
{
    GDCLASS(JarCloudsEffect, CompositorEffect);

  protected:
    static void _bind_methods();

  public:
    struct Layer
    {
        JarCloudParams params{};
        RID shape, detail, weather; // RenderingServer textures
        bool radial = true;
        // Where the shell is drawn (render units): only pixels it may cover are traced.
        Vector3 shell_center;
        float shell_radius = 0.0f;
        Vector3 center; // the body's center as drawn
        float scale = 1.0f;
        Basis body_to_world;
    };
    // The textures a layer's material shows, and a count that changes when
    // they are made again (larger viewports).
    struct Outputs
    {
        RID color, depth;
        uint64_t generation = 0;
    };

    JarCloudsEffect();
    ~JarCloudsEffect() override;
    void _render_callback(int32_t p_effect_callback_type, RenderData *p_render_data) override;

    // Main thread: this frame's values of layer `id` (an instance id), or its end.
    static void submit(uint64_t id, const Layer &layer);
    static void remove(uint64_t id);
    static Outputs outputs(uint64_t id);
    // Main thread: makes sure the compositor that draws `context`'s camera
    // has a JarCloudsEffect.
    static void ensure_attached(Node *context);
    // GPU time of the clouds in a recent frame, in milliseconds (all layers and
    // viewports; a frame or two late).
    static double get_gpu_msec();
};

#endif // JAR_CLOUD_EFFECT_H
