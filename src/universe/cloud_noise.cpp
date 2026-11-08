#include "cloud_noise.h"
#include <algorithm>
#include <cmath>
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
