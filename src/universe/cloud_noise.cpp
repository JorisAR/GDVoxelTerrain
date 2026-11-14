#include "cloud_noise.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <cstdint>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/core/object.hpp>
#include <thread>
#include <vector>

namespace
{
uint32_t hash(int x, int y, int z, uint32_t seed)
{
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ uint32_t(z) * 0xcb1ab31fu ^ seed * 0x9e3779b9u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

float unit(uint32_t h)
{
    return float(h & 0xffffffu) / float(0x1000000);
}

int wrap(int i, int period)
{
    return ((i % period) + period) % period;
}

// Worley (cellular) noise, tileable over `cells` cells per unit: 1 at the
// feature points, falling to 0 a cell away.
float worley(float x, float y, float z, int cells, uint32_t seed)
{
    x *= float(cells);
    y *= float(cells);
    z *= float(cells);
    const int cx = int(std::floor(x)), cy = int(std::floor(y)), cz = int(std::floor(z));
    float best = 1e9f;
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int ix = cx + dx, iy = cy + dy, iz = cz + dz;
                const uint32_t h = hash(wrap(ix, cells), wrap(iy, cells), wrap(iz, cells), seed);
                const float px = float(ix) + unit(h);
                const float py = float(iy) + unit(hash(int(h), 1, 2, seed));
                const float pz = float(iz) + unit(hash(int(h), 3, 4, seed));
                const float d = (px - x) * (px - x) + (py - y) * (py - y) + (pz - z) * (pz - z);
                best = std::min(best, d);
            }
    return 1.0f - std::min(std::sqrt(best), 1.0f);
}

float worley_fbm(float x, float y, float z, int cells, uint32_t seed)
{
    return worley(x, y, z, cells, seed) * 0.625f + worley(x, y, z, cells * 2, seed + 1) * 0.25f +
           worley(x, y, z, cells * 4, seed + 2) * 0.125f;
}

// Gradient (Perlin) noise, tileable over `cells` cells per unit, about -1..1.
float gradient(float x, float y, float z, int cells, uint32_t seed)
{
    x *= float(cells);
    y *= float(cells);
    z *= float(cells);
    const int ix = int(std::floor(x)), iy = int(std::floor(y)), iz = int(std::floor(z));
    const float fx = x - float(ix), fy = y - float(iy), fz = z - float(iz);
    const auto grad = [&](int dx, int dy, int dz) {
        const uint32_t h = hash(wrap(ix + dx, cells), wrap(iy + dy, cells), wrap(iz + dz, cells), seed);
        // 12 edge directions of a cube.
        static const float G[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1},
                                       {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
        const float *g = G[h % 12u];
        return g[0] * (fx - float(dx)) + g[1] * (fy - float(dy)) + g[2] * (fz - float(dz));
    };
    const auto fade = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    const float u = fade(fx), v = fade(fy), w = fade(fz);
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    return lerp(lerp(lerp(grad(0, 0, 0), grad(1, 0, 0), u), lerp(grad(0, 1, 0), grad(1, 1, 0), u), v),
                lerp(lerp(grad(0, 0, 1), grad(1, 0, 1), u), lerp(grad(0, 1, 1), grad(1, 1, 1), u), v), w);
}

float gradient_fbm(float x, float y, float z, int cells, uint32_t seed)
{
    float v = 0.0f, a = 0.5f, total = 0.0f;
    for (int octave = 0; octave < 5; ++octave)
    {
        v += a * gradient(x, y, z, cells << octave, seed + uint32_t(octave));
        total += a;
        a *= 0.5f;
    }
    return v / total;
}

uint8_t to_byte(float v)
{
    return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// One mip level down: 2x2x2 averages (sizes stay powers of two).
std::vector<uint8_t> downsample(const std::vector<uint8_t> &src, int size)
{
    const int half = size / 2;
    std::vector<uint8_t> dst(size_t(half) * half * half * 4);
    for (int z = 0; z < half; ++z)
        for (int y = 0; y < half; ++y)
            for (int x = 0; x < half; ++x)
                for (int c = 0; c < 4; ++c)
                {
                    int sum = 0;
                    for (int k = 0; k < 8; ++k)
                        sum += src[((size_t(z * 2 + (k >> 2)) * size + (y * 2 + ((k >> 1) & 1))) * size +
                                    (x * 2 + (k & 1))) * 4 + c];
                    dst[((size_t(z) * half + y) * half + x) * 4 + c] = uint8_t((sum + 4) / 8);
                }
    return dst;
}

Ref<ImageTexture3D> build()
{
    constexpr int N = CloudNoise::Size;
    std::vector<uint8_t> data(size_t(N) * N * N * 4);
    const int threads = std::max(1, std::min(int(std::thread::hardware_concurrency()), 16));
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&, t] {
            for (int z = t; z < N; z += threads)
                for (int y = 0; y < N; ++y)
                    for (int x = 0; x < N; ++x)
                    {
                        const float px = (float(x) + 0.5f) / N, py = (float(y) + 0.5f) / N, pz = (float(z) + 0.5f) / N;
                        // Perlin-Worley: Perlin fbm remapped above the inverted
                        // Worley fbm, billowy where both agree.
                        const float perlin = gradient_fbm(px, py, pz, 4, 11) * 0.5f + 0.5f;
                        const float cells = worley_fbm(px, py, pz, 4, 21);
                        const float low = cells - 1.0f;
                        const float pw = std::clamp((perlin - low) / (1.0f - low), 0.0f, 1.0f);
                        uint8_t *out = &data[((size_t(z) * N + y) * N + x) * 4];
                        out[0] = to_byte(pw * pw * 0.6f + pw * 0.4f);
                        out[1] = to_byte(worley_fbm(px, py, pz, 8, 31));
                        out[2] = to_byte(worley_fbm(px, py, pz, 16, 41));
                        out[3] = to_byte(worley_fbm(px, py, pz, 32, 51));
                    }
        });
    for (std::thread &w : workers)
        w.join();
    // Each channel stretched to the full range (its 1st to 99th percentile):
    // the noises come out narrow (most of Perlin-Worley within 0.45 - 0.63),
    // and coverage thresholds need values spread evenly over 0 - 1.
    for (int c = 0; c < 4; ++c)
    {
        int histogram[256] = {};
        for (size_t i = size_t(c); i < data.size(); i += 4)
            histogram[data[i]]++;
        const size_t total = data.size() / 4;
        int lo = 0, hi = 255;
        size_t count = 0;
        while (lo < 255 && (count += histogram[lo]) < total / 100)
            ++lo;
        count = 0;
        while (hi > 0 && (count += histogram[hi]) < total / 100)
            --hi;
        if (hi <= lo)
            continue;
        for (size_t i = size_t(c); i < data.size(); i += 4)
            data[i] = to_byte(float(int(data[i]) - lo) / float(hi - lo));
    }
    TypedArray<Image> images;
    std::vector<uint8_t> level = data;
    for (int size = N; size >= 1; size /= 2)
    {
        for (int z = 0; z < size; ++z)
        {
            PackedByteArray slice;
            slice.resize(int64_t(size) * size * 4);
            std::copy(level.begin() + size_t(z) * size * size * 4, level.begin() + size_t(z + 1) * size * size * 4,
                      slice.ptrw());
            images.push_back(Image::create_from_data(size, size, false, Image::FORMAT_RGBA8, slice));
        }
        if (size > 1)
            level = downsample(level, size);
    }
    Ref<ImageTexture3D> texture;
    texture.instantiate();
    texture->create(Image::FORMAT_RGBA8, N, N, N, true, images);
    return texture;
}
} // namespace

Ref<ImageTexture3D> CloudNoise::texture()
{
    // Shared while anything holds it (no static reference: it would outlive
    // the engine at exit).
    static uint64_t s_id = 0;
    if (auto *existing = Object::cast_to<ImageTexture3D>(ObjectDB::get_instance(s_id)))
        return Ref<ImageTexture3D>(existing);
    Ref<ImageTexture3D> texture = build();
    s_id = texture->get_instance_id();
    return texture;
}

// ------------------------------------------------------------------ weather

namespace
{
struct Vortex
{
    glm::vec3 center;
    float radius;   // radians
    float strength; // radians of turn at the center
    float cover;    // extra coverage around it
};

// fbm of the tileable gradient noise over a direction (the unit cube's
// [-1, 1] mapped onto one period): `cells` cells across the planet.
float sphere_fbm(const glm::vec3 &d, int cells, int octaves, uint32_t seed)
{
    const glm::vec3 p = d * 0.5f + 0.5f;
    float v = 0.0f, a = 0.5f, total = 0.0f;
    for (int o = 0; o < octaves; ++o)
    {
        v += a * gradient(p.x, p.y, p.z, cells << o, seed + uint32_t(o) * 17u);
        total += a;
        a *= 0.5f;
    }
    return v / total * 0.5f + 0.5f;
}

glm::vec3 rotate_about(const glm::vec3 &v, const glm::vec3 &axis, float angle)
{
    const float c = std::cos(angle), s = std::sin(angle);
    return v * c + glm::cross(axis, v) * s + axis * glm::dot(axis, v) * (1.0f - c);
}

float smooth(float a, float b, float x)
{
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float gauss(float x, float center, float width)
{
    const float t = (x - center) / width;
    return std::exp(-t * t);
}

// Direction of texel (x, y) of cube face `face` (+X, -X, +Y, -Y, +Z, -Z).
glm::vec3 face_direction(int face, int x, int y, int size)
{
    const float s = 2.0f * (float(x) + 0.5f) / float(size) - 1.0f;
    const float t = 2.0f * (float(y) + 0.5f) / float(size) - 1.0f;
    glm::vec3 d;
    switch (face)
    {
    case 0:
        d = glm::vec3(1.0f, -t, -s);
        break;
    case 1:
        d = glm::vec3(-1.0f, -t, s);
        break;
    case 2:
        d = glm::vec3(s, 1.0f, t);
        break;
    case 3:
        d = glm::vec3(s, -1.0f, -t);
        break;
    case 4:
        d = glm::vec3(s, -t, 1.0f);
        break;
    default:
        d = glm::vec3(-s, -t, -1.0f);
        break;
    }
    return glm::normalize(d);
}
} // namespace

Ref<Cubemap> CloudNoise::weather(int seed, const Vector3 &pole, int size)
{
    return weather_texture(weather_faces(seed, pole, size), size);
}

CloudNoise::WeatherFaces CloudNoise::weather_faces(int seed, const Vector3 &poleIn, int size)
{
    const glm::vec3 pole = glm::normalize(glm::vec3(poleIn.x, poleIn.y, poleIn.z));
    const uint32_t s = uint32_t(seed) * 2654435761u + 7u;
    // Cyclones in both storm belts and a few tropical ones; they turn the
    // clouds around them (opposite ways in the two hemispheres).
    std::vector<Vortex> vortices;
    const glm::vec3 helper = std::abs(pole.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 east0 = glm::normalize(glm::cross(helper, pole));
    const glm::vec3 north0 = glm::cross(pole, east0);
    for (int k = 0; k < 34; ++k)
    {
        const bool tropical = k >= 28;
        const float hemisphere = unit(hash(k, 1, 0, s)) < 0.5f ? -1.0f : 1.0f;
        const float lat = tropical ? 0.18f + 0.15f * unit(hash(k, 2, 0, s)) : 0.5f + 0.32f * unit(hash(k, 2, 0, s));
        const float lon = 6.2831853f * unit(hash(k, 3, 0, s));
        const float r = std::sqrt(1.0f - lat * lat);
        Vortex v;
        v.center = glm::normalize(pole * (lat * hemisphere) + (east0 * std::cos(lon) + north0 * std::sin(lon)) * r);
        v.radius = tropical ? 0.04f + 0.03f * unit(hash(k, 4, 0, s)) : 0.09f + 0.09f * unit(hash(k, 4, 0, s));
        v.strength = hemisphere * (tropical ? 7.0f : 3.0f + 3.0f * unit(hash(k, 5, 0, s)));
        v.cover = tropical ? 0.6f : 0.25f + 0.2f * unit(hash(k, 6, 0, s));
        vortices.push_back(v);
    }
    std::vector<std::vector<uint8_t>> faces(6, std::vector<uint8_t>(size_t(size) * size * 4));
    const int threads = std::max(1, std::min(int(std::thread::hardware_concurrency()), 16));
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&, t] {
            for (int row = t; row < 6 * size; row += threads)
            {
                const int face = row / size, y = row % size;
                uint8_t *out = faces[face].data() + size_t(y) * size * 4;
                for (int x = 0; x < size; ++x)
                {
                    const glm::vec3 d = face_direction(face, x, y, size);
                    // Swirled by the vortices nearby.
                    glm::vec3 q = d;
                    float vortexCover = 0.0f;
                    for (const Vortex &v : vortices)
                    {
                        const float angle = std::acos(std::clamp(glm::dot(q, v.center), -1.0f, 1.0f));
                        if (angle > 3.0f * v.radius)
                            continue;
                        const float k = angle / v.radius;
                        q = rotate_about(q, v.center, v.strength * std::exp(-k * k));
                        const float k2 = angle / (1.4f * v.radius);
                        vortexCover = std::max(vortexCover, v.cover * std::exp(-k2 * k2));
                    }
                    const float lat = glm::dot(d, pole);
                    const float zone = std::abs(lat) + 0.08f * (sphere_fbm(d, 3, 3, s + 11) - 0.5f) * 2.0f;
                    // Drawn out east to west (fronts, bands): the pole-ward
                    // part of the position counts more.
                    const glm::vec3 stretched = glm::normalize(q + pole * glm::dot(q, pole) * 1.6f) * 0.98f;
                    const float systems = sphere_fbm(stretched, 5, 5, s + 23);
                    const float meso = sphere_fbm(q, 16, 6, s + 41);
                    // Fine streaks along the fronts.
                    const float streaks = sphere_fbm(glm::normalize(q + pole * glm::dot(q, pole) * 3.0f) * 0.98f, 40, 4, s + 47);
                    // Fields of scattered heaped cloud (trade cumulus): cells of
                    // a few tens of kilometers, in clusters.
                    const float popcorn = smooth(0.5f, 0.78f, worley(d.x * 0.5f + 0.5f, d.y * 0.5f + 0.5f, d.z * 0.5f + 0.5f, 240, s + 83)) *
                                          smooth(0.42f, 0.62f, sphere_fbm(d, 24, 3, s + 89));
                    const float patches = sphere_fbm(d, 7, 3, s + 59);
                    const float cells = worley(d.x * 0.5f + 0.5f, d.y * 0.5f + 0.5f, d.z * 0.5f + 0.5f, 90, s + 71);

                    const float wItcz = gauss(zone, 0.0f, 0.12f);
                    const float wSub = gauss(zone, 0.4f, 0.14f);
                    const float wMid = gauss(zone, 0.7f, 0.15f);
                    const float wPolar = smooth(0.8f, 0.95f, zone);
                    const float total = wItcz + wSub + wMid + wPolar + 1e-4f;

                    const float itcz = std::max(smooth(0.5f, 0.68f, meso * 0.75f + systems * 0.25f), popcorn * 0.6f);
                    const float stratocumulus = smooth(0.55f, 0.68f, patches) * smooth(0.25f, 0.55f, cells);
                    const float sub = std::max({smooth(0.66f, 0.76f, systems) * 0.7f, stratocumulus * 0.8f, popcorn * 0.55f});
                    const float mid = std::max(smooth(0.44f, 0.62f, systems * 0.6f + meso * 0.3f + streaks * 0.1f + vortexCover * 0.4f),
                                               popcorn * 0.4f);
                    const float polar = 0.35f + 0.5f * smooth(0.35f, 0.65f, systems);
                    float cover = (wItcz * itcz + wSub * sub + wMid * mid + wPolar * polar) / total;
                    cover = std::max(cover, vortexCover * smooth(0.45f, 0.6f, meso + 0.1f) * 1.2f);
                    const float tops = (wItcz * 1.0f + wSub * 0.3f + wMid * (0.55f + 0.35f * mid) + wPolar * 0.4f) / total;
                    const float sheet =
                        (wItcz * 0.05f + wSub * (0.2f + 0.6f * stratocumulus) + wMid * 0.55f + wPolar * 0.8f) / total;
                    out[x * 4 + 0] = to_byte(cover);
                    out[x * 4 + 1] = to_byte(tops);
                    out[x * 4 + 2] = to_byte(sheet);
                    out[x * 4 + 3] = 255;
                }
            }
        });
    for (std::thread &w : workers)
        w.join();
    return faces;
}

Ref<Cubemap> CloudNoise::weather_texture(const WeatherFaces &faces, int size)
{
    TypedArray<Image> images;
    for (int f = 0; f < 6; ++f)
    {
        PackedByteArray bytes;
        bytes.resize(int64_t(faces[f].size()));
        std::copy(faces[f].begin(), faces[f].end(), bytes.ptrw());
        Ref<Image> image = Image::create_from_data(size, size, false, Image::FORMAT_RGBA8, bytes);
        image->generate_mipmaps();
        images.push_back(image);
    }
    Ref<Cubemap> cubemap;
    cubemap.instantiate();
    cubemap->create_from_images(images);
    return cubemap;
}

// ------------------------------------------------------------------- bands

CloudNoise::WeatherFaces CloudNoise::band_faces(int seed, const Vector3 &poleIn, float bandCount, float turbulence,
                                                float storms, int size)
{
    const glm::vec3 pole = glm::normalize(glm::vec3(poleIn.x, poleIn.y, poleIn.z));
    const uint32_t s = uint32_t(seed) * 2246822519u + 13u;
    // The layout: zones and belts of uneven widths (wider near the equator),
    // each with its own darkness, some faded, some coloured.
    const int zones = std::max(4, int(std::round(bandCount)));
    std::vector<float> zoneEdges(static_cast<size_t>(zones) + 1);
    std::vector<float> zoneDark(static_cast<size_t>(zones)), zoneAccent(static_cast<size_t>(zones));
    {
        std::vector<float> widths(static_cast<size_t>(zones));
        float total = 0.0f;
        for (int i = 0; i < zones; ++i)
        {
            const float mid = (float(i) + 0.5f) / float(zones) * 2.0f - 1.0f;
            widths[i] = (0.55f + 0.9f * unit(hash(i, 1, 0, s))) * (1.0f + 0.6f * (1.0f - std::abs(mid)));
            total += widths[i];
        }
        float at = -1.0f;
        for (int i = 0; i < zones; ++i)
        {
            zoneEdges[i] = at;
            at += 2.0f * widths[i] / total;
            const bool belt = (i % 2) == 1;
            const float strength = 0.35f + 0.65f * unit(hash(i, 2, 0, s));
            const bool faded = unit(hash(i, 3, 0, s)) < 0.18f;
            zoneDark[i] = belt ? (faded ? 0.3f : strength) : 0.12f * unit(hash(i, 4, 0, s));
            zoneAccent[i] = belt && unit(hash(i, 5, 0, s)) < 0.45f ? 0.4f + 0.6f * unit(hash(i, 6, 0, s)) : 0.0f;
        }
        zoneEdges[zones] = 1.0f;
    }
    // Eddies along the band zoneEdges (they curl the zoneEdges), and chains of white
    // ovals at a few of them.
    std::vector<Vortex> vortices;
    const glm::vec3 helper = std::abs(pole.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 east0 = glm::normalize(glm::cross(helper, pole));
    const glm::vec3 north0 = glm::cross(pole, east0);
    const auto place = [&](float lat, float lon) {
        const float r = std::sqrt(std::max(1.0f - lat * lat, 0.0f));
        return glm::normalize(pole * lat + (east0 * std::cos(lon) + north0 * std::sin(lon)) * r);
    };
    const int eddies = int(40.0f * turbulence);
    for (int k = 0; k < eddies; ++k)
    {
        const int edge = 1 + int(unit(hash(k, 7, 0, s)) * float(zones - 1));
        Vortex v;
        v.center = place(std::clamp(zoneEdges[std::min(edge, zones - 1)], -0.95f, 0.95f), 6.2831853f * unit(hash(k, 8, 0, s)));
        v.radius = 0.03f + 0.05f * unit(hash(k, 9, 0, s));
        v.strength = (unit(hash(k, 10, 0, s)) < 0.5f ? -1.0f : 1.0f) * (1.5f + 2.5f * unit(hash(k, 11, 0, s)));
        v.cover = 0.0f;
        vortices.push_back(v);
    }
    std::vector<Vortex> ovals;
    const int chains = 1 + int(storms * 3.0f);
    for (int c = 0; c < chains; ++c)
    {
        const int edge = 1 + int(unit(hash(c, 12, 0, s)) * float(zones - 1));
        const float lat = std::clamp(zoneEdges[std::min(edge, zones - 1)] + 0.01f, -0.85f, 0.85f);
        const int count = 3 + int(unit(hash(c, 13, 0, s)) * 6.0f * storms);
        const float start = 6.2831853f * unit(hash(c, 14, 0, s));
        for (int k = 0; k < count; ++k)
        {
            Vortex v;
            v.center = place(lat, start + float(k) * (0.35f + 0.25f * unit(hash(c, k, 15, s))));
            v.radius = 0.018f + 0.02f * unit(hash(c, k, 16, s));
            v.strength = 2.0f;
            v.cover = 1.0f;
            ovals.push_back(v);
        }
    }
    WeatherFaces faces(6, std::vector<uint8_t>(size_t(size) * size * 4));
    const int threads = std::max(1, std::min(int(std::thread::hardware_concurrency()), 16));
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&, t] {
            for (int row = t; row < 6 * size; row += threads)
            {
                const int face = row / size, y = row % size;
                uint8_t *out = faces[face].data() + size_t(y) * size * 4;
                for (int x = 0; x < size; ++x)
                {
                    const glm::vec3 d = face_direction(face, x, y, size);
                    glm::vec3 q = d;
                    for (const Vortex &v : vortices)
                    {
                        const float angle = std::acos(std::clamp(glm::dot(q, v.center), -1.0f, 1.0f));
                        if (angle > 3.0f * v.radius)
                            continue;
                        const float k = angle / v.radius;
                        q = rotate_about(q, v.center, v.strength * std::exp(-k * k));
                    }
                    float oval = 0.0f;
                    for (const Vortex &v : ovals)
                    {
                        const glm::vec3 off = d - v.center;
                        const float ns = glm::dot(off, pole);
                        const float ew = glm::length(off - pole * ns);
                        const float k = std::sqrt(ew * ew * 0.45f + ns * ns * 1.6f) / v.radius;
                        oval = std::max(oval, 1.0f - smooth(0.6f, 1.0f, k));
                    }
                    // Drawn out east to west: the pole-ward part counts more.
                    const glm::vec3 sheared = glm::normalize(q + pole * glm::dot(q, pole) * 5.0f) * 0.98f;
                    const float lat = glm::dot(q, pole);
                    const float warp = (sphere_fbm(sheared, 6, 5, s + 21) - 0.5f) * 0.07f * turbulence +
                                       (sphere_fbm(sheared, 24, 4, s + 31) - 0.5f) * 0.025f * turbulence;
                    const float wl = std::clamp(lat + warp, -1.0f, 0.9999f);
                    int zone = 0;
                    while (zone + 1 < zones && zoneEdges[zone + 1] <= wl)
                        ++zone;
                    // Soft zoneEdges between neighbours.
                    const float lo = zoneEdges[zone], hi = zoneEdges[zone + 1];
                    const float width = std::max(hi - lo, 1e-4f);
                    const float u = (wl - lo) / width;
                    const float soft = 0.18f;
                    float darkness = zoneDark[zone];
                    float tone = zoneAccent[zone];
                    if (u < soft && zone > 0)
                    {
                        const float m = smooth(-soft, soft, u);
                        darkness = zoneDark[zone - 1] + (darkness - zoneDark[zone - 1]) * m;
                        tone = zoneAccent[zone - 1] + (tone - zoneAccent[zone - 1]) * m;
                    }
                    else if (u > 1.0f - soft && zone + 1 < zones)
                    {
                        const float m = smooth(1.0f - soft, 1.0f + soft, u);
                        darkness = darkness + (zoneDark[zone + 1] - darkness) * m;
                        tone = tone + (zoneAccent[zone + 1] - tone) * m;
                    }
                    // Texture inside the bands: sheared filaments.
                    const float filaments = sphere_fbm(sheared, 48, 4, s + 41);
                    darkness = std::clamp(darkness + (filaments - 0.5f) * 0.35f * turbulence, 0.0f, 1.0f);
                    // Festoons: zoneDark blue-grey streamers just off the equator.
                    const float equator = gauss(wl, 0.07f, 0.05f) + gauss(wl, -0.07f, 0.05f);
                    const float festoon = equator * smooth(0.62f, 0.75f, sphere_fbm(sheared, 30, 3, s + 51));
                    out[x * 4 + 0] = to_byte(darkness);
                    out[x * 4 + 1] = to_byte(tone);
                    out[x * 4 + 2] = to_byte(oval);
                    out[x * 4 + 3] = to_byte(festoon);
                }
            }
        });
    for (std::thread &w : workers)
        w.join();
    return faces;
}
