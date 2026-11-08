#include "ring_layers.h"
#include <cmath>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>

namespace
{
const char *RingShader = "res://addons/jar_voxel_terrain/nodes/rings/rings.gdshader";
const char *GasGiantShader = "res://addons/jar_voxel_terrain/nodes/rings/gas_giant.gdshader";
constexpr int ProfileSize = 2048;

// Saturn's rings in Saturn radii: where each part starts, its opacity and color.
struct Band
{
    float from, opacity;
    float r, g, b;
};
constexpr Band Saturn[] = {
    {1.110f, 0.000f, 0.40f, 0.38f, 0.35f}, // inside the rings
    {1.240f, 0.080f, 0.52f, 0.48f, 0.43f}, // C ring
    {1.450f, 0.140f, 0.58f, 0.53f, 0.47f}, // outer C ring, a little denser
    {1.527f, 0.700f, 0.80f, 0.72f, 0.60f}, // B ring
    {1.650f, 0.920f, 0.86f, 0.78f, 0.65f}, // the dense middle of B
    {1.880f, 0.780f, 0.84f, 0.77f, 0.66f}, // outer B
    {1.951f, 0.040f, 0.45f, 0.42f, 0.39f}, // Cassini division
    {2.025f, 0.550f, 0.78f, 0.73f, 0.65f}, // A ring
    {2.212f, 0.020f, 0.45f, 0.42f, 0.39f}, // Encke gap
    {2.217f, 0.500f, 0.76f, 0.72f, 0.64f}, // outer A ring
    {2.268f, 0.000f, 0.45f, 0.42f, 0.39f}, // beyond A
    {2.318f, 0.350f, 0.75f, 0.72f, 0.66f}, // F ring
    {2.322f, 0.000f, 0.45f, 0.42f, 0.39f},
};
constexpr float SaturnInner = 1.24f, SaturnOuter = 2.33f;

float hash(int seed, int i)
{
    uint32_t h = uint32_t(i) * 0x9E3779B1u ^ uint32_t(seed) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return float(h & 0xFFFFFF) / float(0xFFFFFF);
}

// Smooth 1D value noise with a period of one unit.
float noise(int seed, float x)
{
    const int i = int(std::floor(x));
    const float f = x - float(i);
    const float t = f * f * (3.0f - 2.0f * f);
    return hash(seed, i) * (1.0f - t) + hash(seed, i + 1) * t;
}

// A frame with `up` as its y axis.
Basis basis_with_up(const Vector3 &up)
{
    const Vector3 x = up.cross(std::abs(up.x) < 0.9f ? Vector3(1, 0, 0) : Vector3(0, 0, -1)).normalized();
    return Basis(x, up, x.cross(up).normalized());
}

// A flat annulus in the xz plane from radius `inner` to `outer`.
Ref<ArrayMesh> make_annulus(float inner, float outer)
{
    constexpr int Segments = 256, Rings = 4;
    PackedVector3Array vertices, normals;
    PackedInt32Array indices;
    for (int r = 0; r <= Rings; ++r)
    {
        const float radius = inner + (outer - inner) * float(r) / float(Rings);
        for (int s = 0; s < Segments; ++s)
        {
            // A little outside the circle, so the chords never cut into it.
            const float angle = float(Math_TAU) * float(s) / float(Segments);
            const float out = r == Rings ? 1.0f / std::cos(float(Math_PI) / Segments) : 1.0f;
            vertices.push_back(Vector3(std::cos(angle), 0.0f, std::sin(angle)) * radius * out);
            normals.push_back(Vector3(0, 1, 0));
        }
    }
    for (int r = 0; r < Rings; ++r)
        for (int s = 0; s < Segments; ++s)
        {
            const int a = r * Segments + s, b = r * Segments + (s + 1) % Segments;
            const int c = a + Segments, d = b + Segments;
            indices.append_array(PackedInt32Array({a, c, b, b, c, d}));
        }
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = vertices;
    arrays[Mesh::ARRAY_NORMAL] = normals;
    arrays[Mesh::ARRAY_INDEX] = indices;
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    return mesh;
}

// The spin axis in global space (the ring plane's normal): the spin turns the
// body about it, so it is fixed in the frame that carries the body.
Vector3 spin_axis(const JarCelestialBody &body)
{
    Vector3 axis = body.get_spin_axis();
    if (axis.length_squared() < 1e-12f)
        axis = Vector3(0, 1, 0);
    if (const auto *carrier = Object::cast_to<Node3D>(body.get_parent()); carrier && carrier->is_inside_tree())
        axis = carrier->get_global_basis().xform(axis);
    return axis.normalized();
}
} // namespace

// ---------------------------------------------------------------- JarBodyRings

void JarBodyRings::_bind_methods()
{
#define BIND_RINGS(m_name, m_type, m_hint, m_hint_text)                                                               \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarBodyRings::get_##m_name);                                       \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarBodyRings::set_##m_name);                              \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name);
    BIND_RINGS(inner_radius, Variant::FLOAT, PROPERTY_HINT_RANGE, "1,10,0.001,or_greater,suffix:radii");
    BIND_RINGS(outer_radius, Variant::FLOAT, PROPERTY_HINT_RANGE, "1,10,0.001,or_greater,suffix:radii");
    BIND_RINGS(profile, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Gradient");
    BIND_RINGS(ringlets, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
    BIND_RINGS(opacity, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,2,0.01");
    BIND_RINGS(seed, Variant::INT, PROPERTY_HINT_NONE, "");
    BIND_RINGS(material, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial");
#undef BIND_RINGS
    ClassDB::bind_method(D_METHOD("get_active_material"), &JarBodyRings::get_active_material);
    ClassDB::bind_method(D_METHOD("get_profile_texture"), &JarBodyRings::get_profile_texture);
    ClassDB::bind_method(D_METHOD("get_opacity_at", "radius"), &JarBodyRings::get_opacity_at);
}

void JarBodyRings::changed()
{
    _dirty = true;
}

void JarBodyRings::set_inner_radius(float value)
{
    _innerRadius = std::max(1.0f, value);
    _outerRadius = std::max(_outerRadius, _innerRadius + 0.001f);
    _shellRadius = -1.0f;
    changed();
}

void JarBodyRings::set_outer_radius(float value)
{
    _outerRadius = std::max(value, _innerRadius + 0.001f);
    _shellRadius = -1.0f;
    changed();
}

void JarBodyRings::set_profile(const Ref<Gradient> &value)
{
    if (_profile.is_valid() && _profile->is_connected("changed", callable_mp(this, &JarBodyRings::changed)))
        _profile->disconnect("changed", callable_mp(this, &JarBodyRings::changed));
    _profile = value;
    if (_profile.is_valid())
        _profile->connect("changed", callable_mp(this, &JarBodyRings::changed));
    changed();
}

void JarBodyRings::set_ringlets(float value)
{
    _ringlets = std::clamp(value, 0.0f, 1.0f);
    changed();
}

void JarBodyRings::set_opacity(float value)
{
    _opacity = std::max(0.0f, value);
    changed();
}

void JarBodyRings::set_seed(int value)
{
    _seed = value;
    changed();
}

Ref<ShaderMaterial> JarBodyRings::get_active_material()
{
    if (_material.is_valid())
        return _materialCopy.of(_material); // never the user's own (see JarMaterialCopy)
    if (_defaultMaterial.is_null())
        _defaultMaterial = make_material(RingShader, 40); // after water, before clouds and the atmosphere
    return _defaultMaterial;
}

void JarBodyRings::set_material(const Ref<ShaderMaterial> &value)
{
    _material = value;
    if (_shell)
        _shell->set_material_override(get_active_material());
}

Ref<ImageTexture> JarBodyRings::get_profile_texture()
{
    if (!_dirty && _texture.is_valid())
        return _texture;
    _dirty = false;
    PackedByteArray data;
    data.resize(ProfileSize * 4);
    uint8_t *w = data.ptrw();
    for (int i = 0; i < ProfileSize; ++i)
    {
        const float x = (float(i) + 0.5f) / float(ProfileSize); // 0 at the inner edge, 1 at the outer
        Color c;
        if (_profile.is_valid())
            c = _profile->sample(x);
        else
        {
            // Saturn's, stretched over these radii; parts blend over a little.
            const float r = SaturnInner + x * (SaturnOuter - SaturnInner);
            const int n = int(sizeof(Saturn) / sizeof(Saturn[0]));
            int k = 0;
            while (k + 1 < n && Saturn[k + 1].from <= r)
                ++k;
            const Band &a = Saturn[k];
            c = Color(a.r, a.g, a.b, a.opacity);
            if (k + 1 < n)
            {
                const Band &b = Saturn[k + 1];
                constexpr float Edge = 0.0015f; // radii over which neighbouring parts blend
                const float t = std::clamp((r - (b.from - Edge)) / Edge, 0.0f, 1.0f);
                c = c.lerp(Color(b.r, b.g, b.b, b.opacity), t * t * (3.0f - 2.0f * t));
            }
        }
        // Ringlets: fine bands at many scales, as many as the texture holds.
        float fine = 0.0f, weight = 0.0f, amplitude = 1.0f;
        for (float cycles = 24.0f; cycles <= ProfileSize / 3.0f; cycles *= 2.0f, amplitude *= 0.8f)
        {
            fine += amplitude * (noise(_seed + int(cycles), x * cycles) - 0.5f);
            weight += amplitude;
        }
        fine = fine / weight * 2.4f; // about -1 to 1
        const float opacity = std::clamp(c.a * _opacity * (1.0f + _ringlets * fine), 0.0f, 0.995f);
        const float shade = 1.0f + 0.12f * _ringlets * fine;
        w[i * 4 + 0] = uint8_t(std::clamp(c.r * shade, 0.0f, 1.0f) * 255.0f + 0.5f);
        w[i * 4 + 1] = uint8_t(std::clamp(c.g * shade, 0.0f, 1.0f) * 255.0f + 0.5f);
        w[i * 4 + 2] = uint8_t(std::clamp(c.b * shade, 0.0f, 1.0f) * 255.0f + 0.5f);
        w[i * 4 + 3] = uint8_t(opacity * 255.0f + 0.5f);
    }
    Ref<Image> image = Image::create_from_data(ProfileSize, 1, false, Image::FORMAT_RGBA8, data);
    image->generate_mipmaps();
    if (_texture.is_valid() && _texture->get_width() == ProfileSize)
        _texture->update(image);
    else
        _texture = ImageTexture::create_from_image(image);
    return _texture;
}

float JarBodyRings::get_opacity_at(float radius)
{
    const float x = (radius - _innerRadius) / (_outerRadius - _innerRadius);
    if (x < 0.0f || x >= 1.0f)
        return 0.0f;
    const Ref<Image> image = get_profile_texture()->get_image();
    return image->get_pixel(std::min(int(x * ProfileSize), ProfileSize - 1), 0).a;
}

Vector3 JarBodyRings::get_normal(const JarCelestialBody &body) const
{
    return spin_axis(body);
}

void JarBodyRings::build()
{
    _shell = add_shell();
    _shell->set_material_override(get_active_material());
}

void JarBodyRings::update(const JarCelestialBody &body)
{
    if (_shell == nullptr)
        return;
    const JarCelestialBody::Frame frame = body.get_frame();
    if (!frame.radial)
    {
        _shell->set_visible(false);
        return;
    }
    _shell->set_visible(true);
    if (_shellRadius < 0.0f)
    {
        _shell->set_mesh(make_annulus(_innerRadius, _outerRadius));
        _shellRadius = 1.0f;
    }
    body.extend_view_radius(frame.seaLevel * _outerRadius);
    const FarView view = far_view(body);
    const float radius = frame.seaLevel * view.scale;
    const Vector3 normal = spin_axis(body);
    _shell->set_global_transform(Transform3D(basis_with_up(normal).scaled(Vector3(radius, radius, radius)), view.center));
    const Ref<ShaderMaterial> material = get_active_material();
    material->set_shader_parameter("planet_center", view.center);
    material->set_shader_parameter("planet_radius", radius);
    material->set_shader_parameter("ring_normal", normal);
    material->set_shader_parameter("ring_inner", _innerRadius * radius);
    material->set_shader_parameter("ring_outer", _outerRadius * radius);
    material->set_shader_parameter("ring_profile", get_profile_texture());
    apply_star(material, frame.center);
}

// ------------------------------------------------------------- JarBodyGasGiant

void JarBodyGasGiant::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_material"), &JarBodyGasGiant::get_material);
    ClassDB::bind_method(D_METHOD("set_material", "value"), &JarBodyGasGiant::set_material);
    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial"),
                 "set_material", "get_material");
    ClassDB::bind_method(D_METHOD("get_active_material"), &JarBodyGasGiant::get_active_material);
    ClassDB::bind_method(D_METHOD("get_flattening"), &JarBodyGasGiant::get_flattening);
    ClassDB::bind_method(D_METHOD("set_flattening", "value"), &JarBodyGasGiant::set_flattening);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flattening", PROPERTY_HINT_RANGE, "0,0.5,0.001"), "set_flattening",
                 "get_flattening");
}

Ref<ShaderMaterial> JarBodyGasGiant::get_active_material()
{
    if (_material.is_valid())
        return _materialCopy.of(_material); // never the user's own (see JarMaterialCopy)
    if (_defaultMaterial.is_null())
        _defaultMaterial = make_material(GasGiantShader, 0);
    return _defaultMaterial;
}

void JarBodyGasGiant::set_material(const Ref<ShaderMaterial> &value)
{
    _material = value;
    if (_shell)
        _shell->set_material_override(get_active_material());
}

void JarBodyGasGiant::build()
{
    _shell = add_shell();
    Ref<SphereMesh> sphere;
    sphere.instantiate();
    sphere->set_radius(1.0f);
    sphere->set_height(2.0f);
    sphere->set_radial_segments(128);
    sphere->set_rings(64);
    _shell->set_mesh(sphere);
    _shell->set_material_override(get_active_material());
    if (const JarCelestialBody *body = get_body())
    {
        const TypedArray<Node> rings = body->find_children("*", "JarBodyRings", true, false);
        if (!rings.is_empty())
            _rings = Object::cast_to<Node>(rings[0])->get_instance_id();
    }
}

void JarBodyGasGiant::update(const JarCelestialBody &body)
{
    if (_shell == nullptr)
        return;
    const JarCelestialBody::Frame frame = body.get_frame();
    body.extend_view_radius(frame.seaLevel);
    const FarView view = far_view(body);
    const float radius = frame.seaLevel * view.scale;
    // Squashed along the spin axis by the flattening.
    const Vector3 axis = spin_axis(body);
    const Basis frameBasis = basis_with_up(axis);
    const Basis squash = frameBasis * Basis().scaled(Vector3(radius, radius * (1.0f - _flattening), radius)) * frameBasis.transposed();
    _shell->set_global_transform(Transform3D(squash, view.center));
    const Ref<ShaderMaterial> material = get_active_material();
    material->set_shader_parameter("planet_center", view.center);
    material->set_shader_parameter("planet_radius", radius);
    material->set_shader_parameter("flattening", _flattening);
    material->set_shader_parameter("spin_axis_world", axis);
    // Bands and storms turn with the body.
    material->set_shader_parameter("world_to_body", body.get_global_basis().orthonormalized().transposed());
    material->set_shader_parameter("spin_axis", body.get_global_basis().orthonormalized().transposed().xform(spin_axis(body)));
    apply_star(material, frame.center);
    auto *rings = Object::cast_to<JarBodyRings>(ObjectDB::get_instance(_rings));
    material->set_shader_parameter("has_rings", rings != nullptr && rings->is_visible_in_tree());
    if (rings)
    {
        material->set_shader_parameter("ring_normal", rings->get_normal(body));
        material->set_shader_parameter("ring_inner", rings->get_inner_radius() * radius);
        material->set_shader_parameter("ring_outer", rings->get_outer_radius() * radius);
        material->set_shader_parameter("ring_profile", rings->get_profile_texture());
    }
}
