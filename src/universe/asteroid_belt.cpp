#include "asteroid_belt.h"
#include "voxel_universe.h"
#include <cmath>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/multi_mesh.hpp>
#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <algorithm>
#include <map>
#include <thread>

// Near: small rocks around the camera; far: large ones out to hundreds of km.
const JarBodyAsteroids::Tier JarBodyAsteroids::Tiers[2] = {
    {1500.0f, 12000.0f, 5.0f, 150.0f, 2.5f, 3, 1u},
    {30000.0f, 600000.0f, 150.0f, 3000.0f, 1.5f, 2, 2u},
};

namespace
{
uint32_t mix(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

uint32_t hash_cell(const glm::ivec3 &c, uint32_t salt)
{
    return mix(uint32_t(c.x) * 0x8da6b343u ^ uint32_t(c.y) * 0xd8163841u ^ uint32_t(c.z) * 0xcb1ab31fu ^ salt * 0x9e3779b9u);
}

float unit(uint32_t h)
{
    return float(h & 0xffffffu) / float(0x1000000);
}

// Smooth value noise on a sphere point (for the rock shapes).
float value_noise(const glm::vec3 &p, uint32_t seed)
{
    const glm::vec3 i = glm::floor(p);
    const glm::vec3 f = p - i;
    const glm::vec3 u = f * f * (3.0f - 2.0f * f);
    float result = 0.0f;
    for (int k = 0; k < 8; ++k)
    {
        const glm::ivec3 o((k & 1), (k >> 1) & 1, (k >> 2) & 1);
        const float v = unit(hash_cell(glm::ivec3(i) + o, seed));
        const float w = (o.x ? u.x : 1.0f - u.x) * (o.y ? u.y : 1.0f - u.y) * (o.z ? u.z : 1.0f - u.z);
        result += v * w;
    }
    return result;
}

// An icosphere subdivided `detail` times, displaced into a rock: lumpy,
// stretched, with a few craters. Radius about 1.
Ref<ArrayMesh> make_rock(int detail, uint32_t seed)
{
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<glm::vec3> verts = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                                    {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (glm::vec3 &v : verts)
        v = glm::normalize(v);
    std::vector<glm::ivec3> faces = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                                     {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
                                     {3, 8, 9},  {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},   {9, 8, 1}};
    for (int s = 0; s < detail; ++s)
    {
        std::map<std::pair<int, int>, int> middle;
        auto mid = [&](int a, int b) {
            const std::pair<int, int> key(std::min(a, b), std::max(a, b));
            if (auto it = middle.find(key); it != middle.end())
                return it->second;
            verts.push_back(glm::normalize(verts[a] + verts[b]));
            middle[key] = int(verts.size()) - 1;
            return int(verts.size()) - 1;
        };
        std::vector<glm::ivec3> next;
        for (const glm::ivec3 &f : faces)
        {
            const int a = mid(f.x, f.y), b = mid(f.y, f.z), c = mid(f.z, f.x);
            next.push_back({f.x, a, c});
            next.push_back({f.y, b, a});
            next.push_back({f.z, c, b});
            next.push_back({a, b, c});
        }
        faces = std::move(next);
    }
    // Craters: bowls with raised rims.
    struct Crater
    {
        glm::vec3 at;
        float size;
    };
    std::vector<Crater> craters;
    for (uint32_t k = 0; k < 6; ++k)
    {
        const glm::vec3 d(unit(mix(seed + k * 3u)) * 2.0f - 1.0f, unit(mix(seed + k * 3u + 1u)) * 2.0f - 1.0f,
                          unit(mix(seed + k * 3u + 2u)) * 2.0f - 1.0f);
        craters.push_back({glm::normalize(d + glm::vec3(1e-3f)), 0.2f + 0.35f * unit(mix(seed + k * 7u + 5u))});
    }
    const glm::vec3 stretch(1.0f, 0.55f + 0.4f * unit(mix(seed + 101u)), 0.45f + 0.4f * unit(mix(seed + 103u)));
    for (glm::vec3 &v : verts)
    {
        float r = 1.0f;
        float a = 0.5f, f = 1.6f;
        for (int o = 0; o < 4; ++o, a *= 0.5f, f *= 2.1f)
            r += (value_noise(v * f + glm::vec3(float(o) * 7.3f), seed + uint32_t(o)) - 0.5f) * a * 0.6f;
        for (const Crater &c : craters)
        {
            const float d = std::acos(std::clamp(glm::dot(v, c.at), -1.0f, 1.0f)) / c.size;
            if (d < 1.4f)
                r += d < 1.0f ? -0.12f * c.size * (1.0f - d * d) : 0.05f * c.size * (1.4f - d) / 0.4f;
        }
        v = v * r * stretch;
    }
    std::vector<glm::vec3> normals(verts.size(), glm::vec3(0.0f));
    for (const glm::ivec3 &f : faces)
    {
        const glm::vec3 n = glm::cross(verts[f.y] - verts[f.x], verts[f.z] - verts[f.x]);
        normals[f.x] += n;
        normals[f.y] += n;
        normals[f.z] += n;
    }
    PackedVector3Array positions, normalArray;
    PackedInt32Array indices;
    for (size_t i = 0; i < verts.size(); ++i)
    {
        positions.push_back(Vector3(verts[i].x, verts[i].y, verts[i].z));
        const glm::vec3 n = glm::normalize(normals[i]);
        normalArray.push_back(Vector3(n.x, n.y, n.z));
    }
    // Godot's front faces wind clockwise.
    for (const glm::ivec3 &f : faces)
    {
        indices.push_back(f.x);
        indices.push_back(f.z);
        indices.push_back(f.y);
    }
    Array arrays;
    arrays.resize(Mesh::ARRAY_MAX);
    arrays[Mesh::ARRAY_VERTEX] = positions;
    arrays[Mesh::ARRAY_NORMAL] = normalArray;
    arrays[Mesh::ARRAY_INDEX] = indices;
    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
    return mesh;
}

// The belt's density at an offset from its center (any thread).
float belt_profile(const glm::dvec3 &offset, double inner, double outer, double thickness, const glm::dvec3 &axisIn)
{
    const glm::dvec3 axis = glm::normalize(axisIn);
    const double h = glm::dot(offset, axis);
    const double r = glm::length(offset - axis * h);
    const double edge = std::max(1.0, (outer - inner) * 0.25);
    const auto smooth = [](double a, double b, double x) {
        const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    };
    const double radial = smooth(inner, inner + edge, r) * (1.0 - smooth(outer - edge, outer, r));
    const double vertical = std::exp(-(h / thickness) * (h / thickness));
    return float(radial * vertical);
}
} // namespace

void JarBodyAsteroids::_bind_methods()
{
#define BIND_BELT(m_name, m_type, m_hint, m_hint_text)                                                                \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarBodyAsteroids::get_##m_name);                                   \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarBodyAsteroids::set_##m_name);                          \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
    BIND_BELT(inner_radius, Variant::FLOAT, PROPERTY_HINT_NONE, "suffix:m");
    BIND_BELT(outer_radius, Variant::FLOAT, PROPERTY_HINT_NONE, "suffix:m");
    BIND_BELT(thickness, Variant::FLOAT, PROPERTY_HINT_NONE, "suffix:m");
    BIND_BELT(axis, Variant::VECTOR3, PROPERTY_HINT_NONE, "");
    BIND_BELT(density, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,10,0.01,or_greater");
    BIND_BELT(seed, Variant::INT, PROPERTY_HINT_NONE, "");
    BIND_BELT(material, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Material");
#undef BIND_BELT
    ClassDB::bind_method(D_METHOD("get_rock_count"), &JarBodyAsteroids::get_rock_count);
}

Ref<Material> JarBodyAsteroids::active_material()
{
    if (_material.is_valid())
        return _material;
    if (_defaultMaterial.is_null())
    {
        Ref<StandardMaterial3D> rock;
        rock.instantiate();
        rock->set_albedo(Color(0.3f, 0.27f, 0.24f));
        rock->set_roughness(0.95f);
        _defaultMaterial = rock;
    }
    return _defaultMaterial;
}

void JarBodyAsteroids::set_material(const Ref<Material> &value)
{
    _material = value;
    for (TierState &tier : _tiers)
        for (MultiMeshInstance3D *instance : tier.instances)
            if (instance)
                instance->set_material_override(active_material());
}

void JarBodyAsteroids::build()
{
    for (int t = 0; t < 2; ++t)
        for (int v = 0; v < Variants; ++v)
        {
            _meshes[t][v] = make_rock(Tiers[t].detail, uint32_t(_seed) * 977u + uint32_t(v) * 131u);
            auto *instance = memnew(MultiMeshInstance3D);
            instance->set_as_top_level(true);
            instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
            Ref<MultiMesh> multimesh;
            multimesh.instantiate();
            multimesh->set_transform_format(MultiMesh::TRANSFORM_3D);
            multimesh->set_mesh(_meshes[t][v]);
            instance->set_multimesh(multimesh);
            instance->set_material_override(active_material());
            add_child(instance, false, INTERNAL_MODE_FRONT);
            _tiers[t].instances[v] = instance;
        }
}

float JarBodyAsteroids::profile_at(const glm::dvec3 &offset) const
{
    return belt_profile(offset, _innerRadius, _outerRadius, _thickness, DTransform::from(_axis));
}

void JarBodyAsteroids::start_build(int t, const glm::dvec3 &camera, const glm::ivec3 &cell)
{
    auto job = std::make_shared<Built>();
    _tiers[t].job = job;
    _tiers[t].builtCell = cell;
    const Tier tier = Tiers[t];
    const float density = _density;
    const uint32_t seed = uint32_t(_seed) * 7919u + tier.salt;
    const double inner = _innerRadius, outer = _outerRadius, thickness = _thickness;
    const glm::dvec3 axis = DTransform::from(_axis);
    std::thread([job, tier, density, seed, camera, cell, inner, outer, thickness, axis] {
        const glm::dvec3 anchor = glm::dvec3(cell) * double(tier.cell);
        job->anchor = anchor;
        job->buffers.assign(Variants, {});
        const int reach = int(std::ceil(tier.reach / tier.cell));
        for (int z = -reach; z <= reach; ++z)
            for (int y = -reach; y <= reach; ++y)
                for (int x = -reach; x <= reach; ++x)
                {
                    if (x * x + y * y + z * z > reach * reach)
                        continue;
                    const glm::ivec3 c = cell + glm::ivec3(x, y, z);
                    const glm::dvec3 center = (glm::dvec3(c) + 0.5) * double(tier.cell);
                    const float profile = belt_profile(center, inner, outer, thickness, axis);
                    const float expected = tier.perCell * density * profile;
                    if (expected <= 0.001f)
                        continue;
                    uint32_t h = hash_cell(c, seed);
                    const int count = int(expected + unit(h));
                    for (int k = 0; k < count; ++k)
                    {
                        h = mix(h + 0x9e3779b9u);
                        const glm::dvec3 local(unit(mix(h + 1u)), unit(mix(h + 2u)), unit(mix(h + 3u)));
                        const glm::dvec3 at = (glm::dvec3(c) + local) * double(tier.cell) - anchor;
                        // Many small, few large.
                        const float u = unit(mix(h + 4u));
                        const float size = tier.minSize * std::pow(tier.maxSize / tier.minSize, u * u * u);
                        const int variant = int(mix(h + 5u) % uint32_t(Variants));
                        // A random rotation (axis and angle).
                        const glm::vec3 raxis = glm::normalize(glm::vec3(unit(mix(h + 6u)), unit(mix(h + 7u)), unit(mix(h + 8u))) - 0.5f + 1e-4f);
                        const Basis basis = Basis(Vector3(raxis.x, raxis.y, raxis.z), 6.2831853f * unit(mix(h + 9u))).scaled(Vector3(size, size, size));
                        std::vector<float> &b = job->buffers[variant];
                        for (int row = 0; row < 3; ++row)
                        {
                            b.push_back(basis.rows[row].x);
                            b.push_back(basis.rows[row].y);
                            b.push_back(basis.rows[row].z);
                            b.push_back(float(at[row]));
                        }
                    }
                }
        job->done = true;
    }).detach();
}

int JarBodyAsteroids::get_rock_count() const
{
    int count = 0;
    for (const TierState &tier : _tiers)
        for (MultiMeshInstance3D *instance : tier.instances)
            if (instance && instance->get_multimesh().is_valid())
                count += instance->get_multimesh()->get_instance_count();
    return count;
}

void JarBodyAsteroids::update(const JarCelestialBody &body)
{
    JarVoxelUniverse *universe = JarVoxelUniverse::get_singleton();
    Viewport *viewport = is_inside_tree() ? get_viewport() : nullptr;
    const Camera3D *camera = viewport ? viewport->get_camera_3d() : nullptr;
    if (universe == nullptr || !universe->has_bodies() || camera == nullptr || _tiers[0].instances[0] == nullptr)
        return;
    // Everything in the universe's doubles: the belt is 10^8 m from the star.
    const double time = universe->get_time();
    const DTransform view = universe->view_at(time);
    const glm::dvec3 center = universe->universe_transform(&body, time).xform(DTransform::from(body.get_local_center()));
    const glm::dvec3 eye = view.inverse().xform(DTransform::from(camera->get_global_position())) - center;
    const Basis basis = DTransform(view.basis, glm::dvec3(0.0)).to().basis;
    for (int t = 0; t < 2; ++t)
    {
        TierState &state = _tiers[t];
        const glm::ivec3 cell(glm::floor(eye / double(Tiers[t].cell)));
        if (state.job && state.job->done)
        {
            for (int v = 0; v < Variants; ++v)
            {
                const std::vector<float> &b = state.job->buffers[v];
                PackedFloat32Array buffer;
                buffer.resize(int64_t(b.size()));
                std::copy(b.begin(), b.end(), buffer.ptrw());
                Ref<MultiMesh> multimesh = state.instances[v]->get_multimesh();
                multimesh->set_instance_count(int(b.size() / 12));
                if (!b.empty())
                    multimesh->set_buffer(buffer);
            }
            state.anchor = state.job->anchor;
            state.hasAnchor = true;
            state.job.reset();
        }
        if (!state.job && cell != state.builtCell)
            start_build(t, eye, cell);
        if (state.hasAnchor)
        {
            // The anchor in the scene, from doubles: a small number near the camera.
            const Vector3 origin = DTransform::to(view.xform(center + state.anchor));
            for (MultiMeshInstance3D *instance : state.instances)
                instance->set_global_transform(Transform3D(basis, origin));
        }
    }
}
