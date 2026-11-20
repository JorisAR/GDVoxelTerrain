#include "cloud_effect.h"
#include <algorithm>
#include <cstring>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/compositor.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rd_sampler_state.hpp>
#include <godot_cpp/classes/rd_shader_file.hpp>
#include <godot_cpp/classes/rd_shader_spirv.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rd_uniform.hpp>
#include <godot_cpp/classes/render_scene_buffers_rd.hpp>
#include <godot_cpp/classes/render_scene_data.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/uniform_set_cache_rd.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <atomic>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
const char *TraceShader = "res://addons/jar_voxel_terrain/nodes/clouds/clouds_trace.glsl";
const char *ResolveShader = "res://addons/jar_voxel_terrain/nodes/clouds/clouds_resolve.glsl";
const char *ContextName = "jar_clouds"; // a StringName here would be made before Godot is up

using RD = RenderingDevice;
using namespace JarCloudPack;

// A camera's view of one layer (its history, last frame's transforms).
struct ViewState
{
    RID params; // uniform buffer
    uint64_t frame = 0;
    uint64_t used = 0; // the renderer's frame it was last drawn in
    bool has_previous = false;
    Projection prev_view_projection;
    Vector3 prev_center;
    float prev_scale = 1.0f;
    Basis prev_body_to_world;
};

struct LayerState
{
    JarCloudsEffect::Layer layer;
    // Shared by all cameras (each writes before its own transparent pass).
    RID current_color, current_depth, out_color;
    Vector2i size;
    uint64_t generation = 0;
};

struct Retired
{
    RID rid;
    uint64_t frame;
};

// Everything on the render thread, shared by all JarCloudsEffects.
struct Renderer
{
    std::mutex mutex; // outputs, read from the main thread
    std::unordered_map<uint64_t, LayerState> layers;
    std::unordered_map<uint64_t, JarCloudsEffect::Outputs> outputs;
    std::map<std::pair<uint64_t, uint64_t>, ViewState> views; // by viewport buffers and layer
    std::vector<Retired> retired;
    uint64_t frame = 0;
    std::atomic<double> gpu_msec{0.0};
    uint64_t measured_frame = 0;
    RID trace_shader, resolve_shader, trace_pipeline, resolve_pipeline;
    RID linear_repeat, linear_clamp, nearest_clamp;
    RID empty_cube;
    bool failed = false;

    bool ready(RD *rd)
    {
        if (trace_pipeline.is_valid())
            return true;
        if (failed)
            return false;
        const auto load = [&](const char *path) -> RID {
            Ref<RDShaderFile> file = ResourceLoader::get_singleton()->load(path);
            if (file.is_null())
            {
                ERR_PRINT(String("JarCloudsEffect: cannot load ") + path);
                return RID();
            }
            Ref<RDShaderSPIRV> spirv = file->get_spirv();
            if (spirv.is_null() || !spirv->get_stage_compile_error(RD::SHADER_STAGE_COMPUTE).is_empty())
            {
                ERR_PRINT(String("JarCloudsEffect: ") + path + ": " +
                          (spirv.is_valid() ? spirv->get_stage_compile_error(RD::SHADER_STAGE_COMPUTE) : String("no SPIR-V")));
                return RID();
            }
            return rd->shader_create_from_spirv(spirv, path);
        };
        trace_shader = load(TraceShader);
        resolve_shader = load(ResolveShader);
        if (!trace_shader.is_valid() || !resolve_shader.is_valid())
        {
            failed = true;
            return false;
        }
        trace_pipeline = rd->compute_pipeline_create(trace_shader);
        resolve_pipeline = rd->compute_pipeline_create(resolve_shader);
        const auto sampler = [&](RD::SamplerFilter filter, RD::SamplerRepeatMode repeat, bool mips) {
            Ref<RDSamplerState> s;
            s.instantiate();
            s->set_min_filter(filter);
            s->set_mag_filter(filter);
            s->set_mip_filter(mips ? RD::SAMPLER_FILTER_LINEAR : RD::SAMPLER_FILTER_NEAREST);
            s->set_repeat_u(repeat);
            s->set_repeat_v(repeat);
            s->set_repeat_w(repeat);
            return rd->sampler_create(s);
        };
        linear_repeat = sampler(RD::SAMPLER_FILTER_LINEAR, RD::SAMPLER_REPEAT_MODE_REPEAT, true);
        linear_clamp = sampler(RD::SAMPLER_FILTER_LINEAR, RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE, true);
        nearest_clamp = sampler(RD::SAMPLER_FILTER_NEAREST, RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE, false);
        // Bound in place of a weather map not made yet (flat worlds have none).
        Ref<RDTextureFormat> format;
        format.instantiate();
        format->set_texture_type(RD::TEXTURE_TYPE_CUBE);
        format->set_width(1);
        format->set_height(1);
        format->set_array_layers(6);
        format->set_format(RD::DATA_FORMAT_R8G8B8A8_UNORM);
        format->set_usage_bits(RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT);
        TypedArray<PackedByteArray> faces;
        for (int i = 0; i < 6; ++i)
        {
            PackedByteArray texel;
            texel.resize(4);
            texel.fill(0);
            faces.push_back(texel);
        }
        Ref<RDTextureView> view;
        view.instantiate();
        empty_cube = rd->texture_create(format, view, faces);
        return true;
    }

    void free_all(RD *rd)
    {
        for (auto &[id, state] : layers)
            free_layer_textures(rd, state);
        layers.clear();
        for (auto &[key, view] : views)
            if (view.params.is_valid())
                rd->free_rid(view.params);
        views.clear();
        for (const Retired &r : retired)
            if (rd->texture_is_valid(r.rid))
                rd->free_rid(r.rid);
        retired.clear();
        for (RID *rid : {&trace_pipeline, &resolve_pipeline, &trace_shader, &resolve_shader, &linear_repeat,
                         &linear_clamp, &nearest_clamp, &empty_cube})
        {
            if (rid->is_valid())
                rd->free_rid(*rid);
            *rid = RID();
        }
        failed = false;
    }

    void free_layer_textures(RD *rd, LayerState &state)
    {
        for (RID *rid : {&state.current_color, &state.current_depth, &state.out_color})
        {
            if (rid->is_valid() && rd->texture_is_valid(*rid))
                rd->free_rid(*rid);
            *rid = RID();
        }
    }

    RID make_texture(RD *rd, const Vector2i &size, RD::DataFormat format)
    {
        Ref<RDTextureFormat> f;
        f.instantiate();
        f->set_width(size.x);
        f->set_height(size.y);
        f->set_format(format);
        f->set_usage_bits(RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT |
                          RD::TEXTURE_USAGE_CAN_UPDATE_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
                          RD::TEXTURE_USAGE_CAN_COPY_TO_BIT);
        Ref<RDTextureView> view;
        view.instantiate();
        return rd->texture_create(f, view);
    }

    // The layer's shared textures, at least `low` large.
    void ensure_size(RD *rd, uint64_t id, LayerState &state, const Vector2i &low)
    {
        if (state.out_color.is_valid() && state.size.x >= low.x && state.size.y >= low.y)
            return;
        const Vector2i size(std::max(low.x, state.size.x), std::max(low.y, state.size.y));
        // The material still shows the old ones until the main thread takes
        // the new ones: freed a few frames later.
        for (RID rid : {state.current_color, state.current_depth, state.out_color})
            if (rid.is_valid())
                retired.push_back({rid, frame + 4});
        state.current_color = make_texture(rd, size, RD::DATA_FORMAT_R16G16B16A16_SFLOAT);
        state.current_depth = make_texture(rd, size, RD::DATA_FORMAT_R32G32_SFLOAT);
        state.out_color = make_texture(rd, size, RD::DATA_FORMAT_R16G16B16A16_SFLOAT);
        rd->texture_clear(state.out_color, Color(0, 0, 0, 0), 0, 1, 0, 1);
        rd->texture_clear(state.current_depth, Color(1e30f, 1e30f, 0, 0), 0, 1, 0, 1);
        state.size = size;
        state.generation++;
        std::lock_guard<std::mutex> lock(mutex);
        outputs[id] = {state.out_color, state.current_depth, state.generation};
    }

    void free_retired(RD *rd)
    {
        for (size_t i = 0; i < retired.size();)
        {
            if (retired[i].frame > frame)
            {
                ++i;
                continue;
            }
            if (rd->texture_is_valid(retired[i].rid))
                rd->free_rid(retired[i].rid);
            retired[i] = retired.back();
            retired.pop_back();
        }
    }
};

Renderer &renderer()
{
    static Renderer *r = new Renderer(); // never destroyed: freed through RD before exit
    return *r;
}

Ref<RDUniform> uniform(RD::UniformType type, int binding, RID a, RID b = RID())
{
    Ref<RDUniform> u;
    u.instantiate();
    u->set_uniform_type(type);
    u->set_binding(binding);
    u->add_id(a);
    if (b.is_valid())
        u->add_id(b);
    return u;
}

// The rectangle of half resolution pixels a sphere may cover (all of them
// when the camera is inside or it reaches behind the camera).
Rect2i sphere_rect(const Vector3 &center, float radius, const Transform3D &camera, const Projection &projection,
                   const Vector2i &full)
{
    const Vector2i low((full.x + 1) / 2, (full.y + 1) / 2);
    const Rect2i all(Vector2i(), low);
    if (radius <= 0.0f)
        return all;
    const Vector3 c = camera.affine_inverse().xform(center);
    if (c.length() < radius * 1.05f || -c.z < radius * 1.05f)
        return all;
    Vector2 lo(1e9f, 1e9f), hi(-1e9f, -1e9f);
    for (int i = 0; i < 8; ++i)
    {
        const Vector3 corner = c + Vector3(i & 1 ? radius : -radius, i & 2 ? radius : -radius, i & 4 ? radius : -radius);
        const Vector4 clip = projection.xform(Vector4(corner.x, corner.y, corner.z, 1.0f));
        if (clip.w <= 0.0f)
            return all;
        const Vector2 ndc(clip.x / clip.w, clip.y / clip.w);
        lo = lo.min(ndc);
        hi = hi.max(ndc);
    }
    // ndc (as rendered: the projection includes Godot's flip) to pixels,
    // then half resolution, with a margin.
    const int x0 = int(std::floor((lo.x * 0.5f + 0.5f) * full.x)) - 2;
    const int x1 = int(std::ceil((hi.x * 0.5f + 0.5f) * full.x)) + 2;
    const int y0 = int(std::floor((lo.y * 0.5f + 0.5f) * full.y)) - 2;
    const int y1 = int(std::ceil((hi.y * 0.5f + 0.5f) * full.y)) + 2;
    const Vector2i a(std::clamp(x0 / 2, 0, low.x), std::clamp(y0 / 2, 0, low.y));
    const Vector2i b(std::clamp((x1 + 1) / 2 + 1, 0, low.x), std::clamp((y1 + 1) / 2 + 1, 0, low.y));
    return Rect2i(a, (b - a).max(Vector2i()));
}

void apply_submit(uint64_t id, const PackedByteArray &bytes)
{
    Renderer &r = renderer();
    LayerState &state = r.layers[id];
    std::memcpy(&state.layer, bytes.ptr(), sizeof(JarCloudsEffect::Layer));
}

void apply_remove(uint64_t id)
{
    Renderer &r = renderer();
    RD *rd = RenderingServer::get_singleton()->get_rendering_device();
    auto it = r.layers.find(id);
    if (it != r.layers.end())
    {
        if (rd)
            r.free_layer_textures(rd, it->second);
        r.layers.erase(it);
    }
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        r.outputs.erase(id);
    }
    if (rd)
    {
        for (auto it = r.views.begin(); it != r.views.end();)
        {
            if (it->first.second != id)
            {
                ++it;
                continue;
            }
            if (it->second.params.is_valid())
                rd->free_rid(it->second.params);
            it = r.views.erase(it);
        }
        if (r.layers.empty())
            r.free_all(rd);
    }
}
} // namespace

void JarCloudsEffect::_bind_methods()
{
    ClassDB::bind_static_method("JarCloudsEffect", D_METHOD("get_gpu_msec"), &JarCloudsEffect::get_gpu_msec);
}

double JarCloudsEffect::get_gpu_msec()
{
    return renderer().gpu_msec.load();
}

JarCloudsEffect::JarCloudsEffect()
{
    set_effect_callback_type(EFFECT_CALLBACK_TYPE_PRE_TRANSPARENT);
    set_access_resolved_depth(true);
}

JarCloudsEffect::~JarCloudsEffect()
{
}

void JarCloudsEffect::submit(uint64_t id, const Layer &layer)
{
    PackedByteArray bytes;
    bytes.resize(sizeof(Layer));
    std::memcpy(bytes.ptrw(), &layer, sizeof(Layer));
    RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(&apply_submit).bind(id, bytes));
}

void JarCloudsEffect::remove(uint64_t id)
{
    if (RenderingServer *rs = RenderingServer::get_singleton())
        rs->call_on_render_thread(callable_mp_static(&apply_remove).bind(id));
}

JarCloudsEffect::Outputs JarCloudsEffect::outputs(uint64_t id)
{
    Renderer &r = renderer();
    std::lock_guard<std::mutex> lock(r.mutex);
    auto it = r.outputs.find(id);
    return it == r.outputs.end() ? Outputs{} : it->second;
}

void JarCloudsEffect::ensure_attached(Node *context)
{
    if (context == nullptr || !context->is_inside_tree())
        return;
    static uint64_t s_checked_frame = 0;
    const uint64_t now = Engine::get_singleton()->get_process_frames();
    if (s_checked_frame != 0 && now - s_checked_frame < 30)
        return;
    s_checked_frame = now;
    const auto has_effect = [](const Ref<Compositor> &compositor) {
        const TypedArray<CompositorEffect> effects = compositor->get_compositor_effects();
        for (int64_t i = 0; i < effects.size(); ++i)
            if (Object::cast_to<JarCloudsEffect>(effects[i]))
                return true;
        return false;
    };
    const auto add_to = [&](Ref<Compositor> compositor) {
        if (has_effect(compositor))
            return;
        TypedArray<CompositorEffect> effects = compositor->get_compositor_effects();
        Ref<JarCloudsEffect> effect;
        effect.instantiate();
        effects.push_back(effect);
        compositor->set_compositor_effects(effects);
    };
    Viewport *viewport = context->get_viewport();
    Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
    if (camera && camera->get_compositor().is_valid())
    {
        add_to(camera->get_compositor());
        return;
    }
    SceneTree *tree = context->get_tree();
    Node *root = tree == nullptr ? nullptr
                 : Engine::get_singleton()->is_editor_hint() ? tree->get_edited_scene_root()
                                                             : tree->get_current_scene();
    if (root == nullptr)
        root = tree ? tree->get_root() : nullptr;
    WorldEnvironment *environment = nullptr;
    if (root)
    {
        const TypedArray<Node> found = root->find_children("*", "WorldEnvironment", true, false);
        if (!found.is_empty())
            environment = Object::cast_to<WorldEnvironment>(found[0]);
    }
    if (environment)
    {
        Ref<Compositor> compositor = environment->get_compositor();
        if (compositor.is_null())
        {
            compositor.instantiate();
            environment->set_compositor(compositor);
        }
        add_to(compositor);
    }
    else if (camera)
    {
        Ref<Compositor> compositor;
        compositor.instantiate();
        camera->set_compositor(compositor);
        add_to(compositor);
    }
}

void JarCloudsEffect::_render_callback(int32_t p_effect_callback_type, RenderData *p_render_data)
{
    if (p_effect_callback_type != EFFECT_CALLBACK_TYPE_PRE_TRANSPARENT || p_render_data == nullptr)
        return;
    Renderer &r = renderer();
    if (r.layers.empty())
        return;
    RD *rd = RenderingServer::get_singleton()->get_rendering_device();
    if (rd == nullptr || !r.ready(rd))
        return;
    Ref<RenderSceneBuffersRD> buffers = p_render_data->get_render_scene_buffers();
    RenderSceneData *scene = p_render_data->get_render_scene_data();
    if (buffers.is_null() || scene == nullptr || buffers->get_view_count() != 1)
        return;
    const Vector2i full = buffers->get_internal_size();
    if (full.x <= 0 || full.y <= 0)
        return;
    const uint64_t buffers_id = buffers->get_instance_id();
    const StringName Context(ContextName);
    const Vector2i low((full.x + 1) / 2, (full.y + 1) / 2);
    const Transform3D camera = scene->get_cam_transform();
    const Projection projection = scene->get_cam_projection();
    const Projection view_projection = projection * Projection(camera.affine_inverse());
    const RID depth = buffers->get_depth_layer(0, false);
    r.frame++;
    r.free_retired(rd);
    // The time between our timestamps in the last frame the GPU finished.
    if (rd->get_captured_timestamps_frame() != r.measured_frame)
    {
        r.measured_frame = rd->get_captured_timestamps_frame();
        uint64_t total = 0, begin = 0;
        for (uint32_t i = 0; i < rd->get_captured_timestamps_count(); ++i)
        {
            const String name = rd->get_captured_timestamp_name(i);
            if (name == "jar_clouds_begin")
                begin = rd->get_captured_timestamp_gpu_time(i);
            else if (name == "jar_clouds_end" && begin != 0)
            {
                total += rd->get_captured_timestamp_gpu_time(i) - begin;
                begin = 0;
            }
        }
        r.gpu_msec = double(total) / 1.0e6; // nanoseconds
    }
    rd->capture_timestamp("jar_clouds_begin");
    // Views of viewports that stopped drawing (closed, or the layer gone).
    for (auto it = r.views.begin(); it != r.views.end();)
    {
        if (r.frame - it->second.used < 2000)
        {
            ++it;
            continue;
        }
        if (it->second.params.is_valid())
            rd->free_rid(it->second.params);
        it = r.views.erase(it);
    }

    for (auto &[id, state] : r.layers)
    {
        const Layer &layer = state.layer;
        if (!layer.shape.is_valid() || !layer.detail.is_valid())
            continue;
        const RID shape = RenderingServer::get_singleton()->texture_get_rd_texture(layer.shape);
        const RID detail = RenderingServer::get_singleton()->texture_get_rd_texture(layer.detail);
        RID weather = layer.weather.is_valid() ? RenderingServer::get_singleton()->texture_get_rd_texture(layer.weather) : RID();
        if (!shape.is_valid() || !detail.is_valid())
            continue;
        if (!weather.is_valid())
            weather = r.empty_cube;
        RID weather_next = layer.weather_next.is_valid() ? RenderingServer::get_singleton()->texture_get_rd_texture(layer.weather_next) : RID();
        if (!weather_next.is_valid())
            weather_next = r.empty_cube;
        r.ensure_size(rd, id, state, low);

        // This camera's view of the layer.
        ViewState &view = r.views[{buffers_id, id}];
        view.used = r.frame;
        if (!view.params.is_valid())
            view.params = rd->uniform_buffer_create(sizeof(JarCloudParams));
        const String suffix = String::num_uint64(id);
        // History (ping-pong): the clouds, and where each ray ended.
        const StringName names[2] = {StringName(String("history_a_") + suffix), StringName(String("history_b_") + suffix)};
        const StringName ends[2] = {StringName(String("end_a_") + suffix), StringName(String("end_b_") + suffix)};
        bool history = view.has_previous;
        for (int i = 0; i < 4; ++i)
        {
            const StringName &name = i < 2 ? names[i] : ends[i - 2];
            if (buffers->has_texture(Context, name))
                continue;
            buffers->create_texture(Context, name, i < 2 ? RD::DATA_FORMAT_R16G16B16A16_SFLOAT : RD::DATA_FORMAT_R32_SFLOAT,
                                    RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT, RD::TEXTURE_SAMPLES_1,
                                    low, 1, 1, true, false);
            history = false;
        }
        const RID history_read = buffers->get_texture(Context, names[view.frame & 1]);
        const RID history_write = buffers->get_texture(Context, names[(view.frame + 1) & 1]);
        const RID end_read = buffers->get_texture(Context, ends[view.frame & 1]);
        const RID end_write = buffers->get_texture(Context, ends[(view.frame + 1) & 1]);

        JarCloudParams params = layer.params;
        put(params.inv_projection, projection.inverse());
        put(params.camera, camera);
        if (view.has_previous)
        {
            put(params.prev_view_projection, view.prev_view_projection);
            put(params.prev_body_to_world, view.prev_body_to_world);
            put4(params.prev_center_scale, view.prev_center, view.prev_scale);
        }
        else
        {
            put(params.prev_view_projection, view_projection);
            put(params.prev_body_to_world, layer.body_to_world);
            put4(params.prev_center_scale, layer.center, layer.scale);
        }
        params.detail_offset[3] = history ? 1.0f : 0.0f;
        const Rect2i rect = layer.radial ? sphere_rect(layer.shell_center, layer.shell_radius, camera, projection, full)
                                         : Rect2i(Vector2i(), low);
        params.sizes[0] = low.x;
        params.sizes[1] = low.y;
        params.sizes[2] = full.x;
        params.sizes[3] = full.y;
        params.rect[0] = rect.position.x;
        params.rect[1] = rect.position.y;
        params.rect[2] = rect.position.x + rect.size.x;
        params.rect[3] = rect.position.y + rect.size.y;
        // The pixel of each 2x2 block this frame: all four in four frames.
        static const int Order[4][2] = {{0, 0}, {1, 1}, {1, 0}, {0, 1}};
        params.frame[0] = int32_t(view.frame & 0x7fffffff);
        params.frame[1] = Order[view.frame & 3][0];
        params.frame[2] = Order[view.frame & 3][1];
        params.frame[3] = 0;
        PackedByteArray bytes;
        bytes.resize(sizeof(JarCloudParams));
        std::memcpy(bytes.ptrw(), &params, sizeof(JarCloudParams));
        rd->buffer_update(view.params, 0, sizeof(JarCloudParams), bytes);

        if (rect.size.x > 0 && rect.size.y > 0)
        {
            TypedArray<RDUniform> uniforms;
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, view.params));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, r.linear_repeat, shape));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, r.linear_repeat, detail));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, r.linear_clamp, weather));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 4, r.nearest_clamp, depth));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_IMAGE, 5, state.current_color));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_IMAGE, 6, state.current_depth));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 7, r.linear_clamp, weather_next));
            const RID set = UniformSetCacheRD::get_cache(r.trace_shader, 0, uniforms);
            const int64_t list = rd->compute_list_begin();
            rd->compute_list_bind_compute_pipeline(list, r.trace_pipeline);
            rd->compute_list_bind_uniform_set(list, set, 0);
            rd->compute_list_dispatch(list, uint32_t((rect.size.x + 7) / 8), uint32_t((rect.size.y + 7) / 8), 1);
            rd->compute_list_end();
        }
        {
            TypedArray<RDUniform> uniforms;
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, view.params));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, r.nearest_clamp, state.current_color));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, r.nearest_clamp, state.current_depth));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, r.linear_clamp, history_read));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_IMAGE, 4, history_write));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 6, r.nearest_clamp, end_read));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_IMAGE, 7, end_write));
            uniforms.push_back(uniform(RD::UNIFORM_TYPE_IMAGE, 5, state.out_color));
            const RID set = UniformSetCacheRD::get_cache(r.resolve_shader, 0, uniforms);
            const int64_t list = rd->compute_list_begin();
            rd->compute_list_bind_compute_pipeline(list, r.resolve_pipeline);
            rd->compute_list_bind_uniform_set(list, set, 0);
            // The traced rectangle and 2 pixels around it (clouds_resolve.glsl).
            const Vector2i from = (rect.position - Vector2i(2, 2)).max(Vector2i());
            const Vector2i to = (rect.position + rect.size + Vector2i(2, 2)).min(low);
            const Vector2i span = (to - from).max(Vector2i());
            rd->compute_list_dispatch(list, uint32_t((span.x + 7) / 8), uint32_t((span.y + 7) / 8), 1);
            rd->compute_list_end();
        }

        view.prev_view_projection = view_projection;
        view.prev_center = layer.center;
        view.prev_scale = layer.scale;
        view.prev_body_to_world = layer.body_to_world;
        view.has_previous = true;
        view.frame++;
    }
    rd->capture_timestamp("jar_clouds_end");
}
