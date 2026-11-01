#include "terrain_sdf.h"
#include <cmath>
#include <cstdint>

// Erosion filter after Rune Skovbo Johansen (runevision), "Fast and Gorgeous
// Erosion Filter" (2026), itself building on Clay John's "Eroded Terrain Noise"
// and Felix Westin's (Fewes) "Terrain Erosion Noise". See the header.

namespace
{
constexpr float Tau = 6.28318530718f;

float ease_out(float t)
{
    const float v = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - v * v;
}

// |x| rounded off within about `soft` of zero (|x| when soft is 0), still 1 at 1.
float soft_abs(float x, float soft)
{
    if (soft <= 0.0f)
        return std::abs(x);
    return (std::sqrt(x * x + soft * soft) - soft) / (std::sqrt(1.0f + soft * soft) - soft);
}

// Flip, raise to a power, flip back.
float pow_inv(float t, float power)
{
    return 1.0f - std::pow(1.0f - std::clamp(t, 0.0f, 1.0f), power);
}

uint32_t hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// sin(x) on [-pi/2, pi/2] (Taylor to x^9: error below 4e-6).
inline float sin_half(float x)
{
    const float x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f + x2 * (1.0f / 362880.0f)))));
}

// sin and cos of any angle, several times faster than std::sin + std::cos
// (the erosion's stripes take a few dozen per sample).
inline void fast_sincos(float angle, float &s, float &c)
{
    constexpr float Pi = 3.14159265359f, HalfPi = 1.57079632679f;
    // Wrap to [-pi, pi], then fold into [-pi/2, pi/2] where the series is accurate.
    float x = angle - Tau * std::floor(angle * (1.0f / Tau) + 0.5f);
    const float sx = x > HalfPi ? Pi - x : (x < -HalfPi ? -Pi - x : x);
    float y = x + HalfPi; // cos(x) = sin(x + pi/2)
    y = y > Pi ? y - Tau : y;
    const float cy = y > HalfPi ? Pi - y : (y < -HalfPi ? -Pi - y : y);
    s = sin_half(sx);
    c = sin_half(cy);
}

// Random point inside a grid cell, in [0, 1)^3 (10 bits of one hash each).
glm::vec3 cell_offset(const glm::ivec3 &cell)
{
    const uint32_t h = hash_u32(uint32_t(cell.x) * 0x8da6b343U ^ uint32_t(cell.y) * 0xd8163841U ^ uint32_t(cell.z) * 0xcb1ab31fU);
    constexpr float Scale = 1.0f / 1024.0f;
    return glm::vec3(float(h & 0x3FF), float((h >> 10) & 0x3FF), float((h >> 20) & 0x3FF)) * Scale;
}
} // namespace

void JarTerrainSdf::frame(const glm::vec3 &pos, glm::dvec3 &surface, glm::vec3 &up, float &above) const
{
    if (_shape == SHAPE_SPHERE)
    {
        // In doubles: |pos| - radius in floats lost the height to rounding on
        // large planets (a float step is 6 cm at 600 km, random per sample).
        const glm::dvec3 p(pos);
        const double len = glm::length(p);
        const glm::dvec3 u = len > 1e-6 ? p / len : glm::dvec3(0.0, 1.0, 0.0);
        up = glm::vec3(u);
        surface = u * double(_radius);
        above = float(len - double(_radius));
    }
    else
    {
        up = glm::vec3(0.0f, 1.0f, 0.0f);
        surface = glm::dvec3(pos.x, 0.0, pos.z);
        above = pos.y;
    }
}

float JarTerrainSdf::noise_value(const Noise &noise, const glm::dvec3 &surface) const
{
    if (noise.jar.is_valid())
        return noise.jar->sample(_shape == SHAPE_SPHERE ? surface : glm::dvec3(surface.x, 0.0, surface.z)).value;
    // FastNoiseLite takes floats only.
    return _shape == SHAPE_SPHERE ? noise.fast->get_noise_3d(real_t(surface.x), real_t(surface.y), real_t(surface.z))
                                  : noise.fast->get_noise_2d(real_t(surface.x), real_t(surface.z));
}

float JarTerrainSdf::layered_height(const glm::dvec3 &surface) const
{
    if (analytic())
        return analytic_layers(surface, 0.0f).height;
    float h = 0.0f;
    if (_noise.is_valid())
    {
        const float n = noise_value(_noise, surface);
        h = n > 0.0f ? n * _height : n * _depth;
    }
    if (_mountainNoise.is_valid() && _mountainHeight > 0.0f)
    {
        // Where: the mask (or the mountain noise itself); how high: the
        // mountain noise's shape, when a mask is given.
        const float m = noise_value(_mountainMask.is_valid() ? _mountainMask : _mountainNoise, surface);
        const float t = std::clamp((m - _mountainThreshold) / _mountainBlend, 0.0f, 1.0f);
        const float shape = _mountainMask.is_valid() ? 0.5f * (noise_value(_mountainNoise, surface) + 1.0f) : 1.0f;
        h += _mountainHeight * t * t * (3.0f - 2.0f * t) * shape;
    }
    return h;
}

JarTerrainSdf::Layers JarTerrainSdf::analytic_layers(const glm::dvec3 &surface, float broadWavelength) const
{
    const glm::dvec3 p = _shape == SHAPE_SPHERE ? surface : glm::dvec3(surface.x, 0.0, surface.z);
    Layers layers;
    if (_noise.jar.is_valid())
    {
        const JarNoise::Sample n = _noise.jar->sample(p, broadWavelength);
        const float scale = n.value > 0.0f ? _height : _depth;
        layers.height = n.value * scale;
        layers.gradient = n.gradient * scale;
        layers.broad = n.broad * scale;
    }
    if (_mountainNoise.jar.is_valid() && _mountainHeight > 0.0f)
    {
        const bool masked = _mountainMask.jar.is_valid();
        const JarNoise::Sample m = (masked ? _mountainMask.jar : _mountainNoise.jar)->sample(p, broadWavelength);
        const float raw = (m.value - _mountainThreshold) / _mountainBlend;
        const float t = std::clamp(raw, 0.0f, 1.0f);
        const float rise = t * t * (3.0f - 2.0f * t);
        const float riseSlope = raw > 0.0f && raw < 1.0f ? 6.0f * t * (1.0f - t) / _mountainBlend : 0.0f;
        float shape = 1.0f;
        glm::vec3 shapeGradient(0.0f), shapeBroad(0.0f);
        if (masked)
        {
            const JarNoise::Sample s = _mountainNoise.jar->sample(p, broadWavelength);
            shape = 0.5f * (s.value + 1.0f);
            shapeGradient = 0.5f * s.gradient;
            shapeBroad = 0.5f * s.broad;
        }
        layers.height += _mountainHeight * rise * shape;
        layers.gradient += _mountainHeight * (riseSlope * shape * m.gradient + rise * shapeGradient);
        layers.broad += _mountainHeight * (riseSlope * shape * m.broad + rise * shapeBroad);
    }
    return layers;
}

float JarTerrainSdf::max_layered_height() const
{
    return std::max(std::abs(_height), std::abs(_depth)) + (_mountainNoise.is_valid() ? _mountainHeight : 0.0f);
}

glm::vec3 JarTerrainSdf::height_gradient(const glm::dvec3 &surface, const glm::vec3 &up, float h, float step) const
{
    if (analytic())
    {
        const glm::vec3 g = analytic_layers(surface, 0.0f).gradient;
        return g - up * glm::dot(g, up);
    }
    // Forward differences along two tangent directions: the local slope.
    const glm::vec3 t1 = glm::normalize(std::abs(up.y) < 0.9f ? glm::cross(up, glm::vec3(0, 1, 0))
                                                               : glm::cross(up, glm::vec3(1, 0, 0)));
    const glm::vec3 t2 = glm::cross(up, t1);
    const float e = step;
    auto at = [&](const glm::vec3 &offset) {
        glm::dvec3 p = surface + glm::dvec3(offset);
        if (_shape == SHAPE_SPHERE)
            p = glm::normalize(p) * double(_radius);
        return layered_height(p);
    };
    return t1 * ((at(t1 * e) - h) / e) + t2 * ((at(t2 * e) - h) / e);
}

// Low-passed slope of the layers: tetrahedral differences `step` apart in
// fixed world directions, projected on the tangent plane. Unlike differences
// along a tangent frame (which must switch axes somewhere on a sphere), this
// is continuous everywhere: the frame switch left a ~2 m step in the eroded
// terrain along two circles of latitude.
glm::vec3 JarTerrainSdf::broad_gradient(const glm::dvec3 &surface, const glm::vec3 &up, float step) const
{
    if (analytic())
    {
        // The octaves at least two steps long: what the differences below
        // keep (they average out anything shorter).
        const glm::vec3 g = analytic_layers(surface, 2.0f * step).broad;
        return g - up * glm::dot(g, up);
    }
    const float d = step / std::sqrt(3.0f);
    static const glm::vec3 corners[4] = {glm::vec3(1, -1, -1), glm::vec3(-1, -1, 1), glm::vec3(-1, 1, -1),
                                         glm::vec3(1, 1, 1)};
    glm::vec3 sum(0.0f);
    for (const glm::vec3 &c : corners)
    {
        glm::dvec3 p = surface + glm::dvec3(c * d);
        if (_shape == SHAPE_SPHERE)
            p = glm::normalize(p) * double(_radius);
        else
            p.y = 0.0;
        sum += c * layered_height(p);
    }
    const glm::vec3 g = sum / (4.0f * d);
    return g - up * glm::dot(g, up);
}

// How the low-passed slope changes sideways (it steers the gullies and sets
// the first fade), for the slope distance() normalizes by; it never changes
// the height. It varies over the erosion scale, so it is computed on a lattice
// 1/4 of that apart, cached per thread, and blended trilinearly (continuous:
// a jump would show in distances and their gradients). Differencing it at
// every sample took 8 more noise samples each.
glm::mat3 JarTerrainSdf::broad_jacobian(const glm::dvec3 &surface, float step) const
{
    const float spacing = _erosionScale;
    const glm::dvec3 g = surface / double(spacing);
    const glm::ivec3 base = glm::ivec3(glm::floor(g));
    const glm::vec3 f(g - glm::floor(g)); // in the cell, from doubles
    const bool flat = _shape != SHAPE_SPHERE;
    // Consecutive samples nearly always fall in the same lattice cell: keep
    // its corners per thread (8 cache lookups per sample were a fifth of the
    // terrain's cost).
    struct Cell
    {
        uint64_t generation = 0;
        glm::ivec3 base{0};
        glm::mat3 corners[8];
    };
    thread_local Cell cell;
    const uint64_t generation = _generation.load(std::memory_order_relaxed);
    if (cell.generation != generation || cell.base != base)
    {
        cell.generation = generation;
        cell.base = base;
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::ivec3 o((corner >> 0) & 1, (corner >> 1) & 1, (corner >> 2) & 1);
            // Flat ground: surface.y is always 0, one layer of the lattice.
            cell.corners[corner] = flat && o.y != 0 ? glm::mat3(0.0f) : lattice_jacobian(base + o, spacing, step);
        }
    }
    glm::mat3 result(0.0f);
    for (int corner = 0; corner < 8; ++corner)
    {
        const glm::ivec3 o((corner >> 0) & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        if (flat && o.y != 0)
            continue;
        const float w = (o.x ? f.x : 1.0f - f.x) * (flat ? 1.0f : (o.y ? f.y : 1.0f - f.y)) * (o.z ? f.z : 1.0f - f.z);
        result += w * cell.corners[corner];
    }
    return result;
}

namespace
{
// Lattice Jacobians shared by all threads: each worker would otherwise compute
// the same points again (12 noise samples each). Direct mapped, one seqlock
// per slot: a reader that races a writer just misses. The generation (unique
// per SDF state) is part of the key, so one table serves every terrain.
struct SharedJacobian
{
    std::atomic<uint32_t> sequence{0}; // odd while being written
    std::atomic<uint64_t> generation{0};
    std::atomic<int32_t> point[3] = {};
    std::atomic<float> values[9] = {};
};
constexpr uint32_t SharedSize = 1u << 18;
SharedJacobian s_sharedJacobians[SharedSize];

bool shared_lookup(uint32_t hash, uint64_t generation, const glm::ivec3 &point, glm::mat3 &out)
{
    SharedJacobian &slot = s_sharedJacobians[hash % SharedSize];
    const uint32_t before = slot.sequence.load(std::memory_order_acquire);
    if (before & 1u)
        return false;
    if (slot.generation.load(std::memory_order_relaxed) != generation ||
        slot.point[0].load(std::memory_order_relaxed) != point.x ||
        slot.point[1].load(std::memory_order_relaxed) != point.y ||
        slot.point[2].load(std::memory_order_relaxed) != point.z)
        return false;
    for (int i = 0; i < 9; ++i)
        out[i / 3][i % 3] = slot.values[i].load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    return slot.sequence.load(std::memory_order_relaxed) == before;
}

void shared_store(uint32_t hash, uint64_t generation, const glm::ivec3 &point, const glm::mat3 &value)
{
    SharedJacobian &slot = s_sharedJacobians[hash % SharedSize];
    uint32_t sequence = slot.sequence.load(std::memory_order_relaxed);
    if ((sequence & 1u) || !slot.sequence.compare_exchange_strong(sequence, sequence + 1, std::memory_order_acquire))
        return; // another thread is writing this slot
    std::atomic_thread_fence(std::memory_order_release);
    slot.generation.store(generation, std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i)
        slot.point[i].store(point[i], std::memory_order_relaxed);
    for (int i = 0; i < 9; ++i)
        slot.values[i].store(value[i / 3][i % 3], std::memory_order_relaxed);
    slot.sequence.store(sequence + 2, std::memory_order_release);
}
} // namespace

// The slope's Jacobian at a lattice point (projected onto the surface).
glm::mat3 JarTerrainSdf::lattice_jacobian(const glm::ivec3 &point, float spacing, float step) const
{
    // A small per-thread cache in front of the shared table (no atomics).
    struct Entry
    {
        uint64_t generation = 0;
        glm::ivec3 point{0};
        glm::mat3 jacobian{0.0f};
    };
    constexpr int Size = 1024;
    thread_local Entry cache[Size];

    const uint32_t hash = (uint32_t(point.x) * 73856093u) ^ (uint32_t(point.y) * 19349663u) ^ (uint32_t(point.z) * 83492791u);
    Entry &entry = cache[hash % Size];
    const uint64_t generation = _generation.load(std::memory_order_relaxed);
    if (entry.generation == generation && entry.point == point)
        return entry.jacobian;
    entry.generation = generation;
    entry.point = point;
    if (shared_lookup(hash, generation, point, entry.jacobian))
        return entry.jacobian;

    glm::dvec3 center = glm::dvec3(point) * double(spacing);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    place_on_surface(center, up);
    entry.jacobian = direct_jacobian(center, up, step);
    shared_store(hash, generation, point, entry.jacobian);
    return entry.jacobian;
}

// Moves `s` onto the base surface (plane or sphere) and sets `u` to the up
// direction there.
void JarTerrainSdf::place_on_surface(glm::dvec3 &s, glm::vec3 &u) const
{
    if (_shape == SHAPE_SPHERE)
    {
        const double len = glm::length(s);
        const glm::dvec3 unit = len > 1e-6 ? s / len : glm::dvec3(0.0, 1.0, 0.0);
        u = glm::vec3(unit);
        s = unit * double(_radius);
    }
    else
    {
        s.y = 0.0;
        u = glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

// The low-passed slope's Jacobian at a surface point, by differences.
glm::mat3 JarTerrainSdf::direct_jacobian(const glm::dvec3 &center, const glm::vec3 &up, float step) const
{
    const glm::vec3 t1 = glm::normalize(std::abs(up.y) < 0.9f ? glm::cross(up, glm::vec3(0, 1, 0))
                                                               : glm::cross(up, glm::vec3(1, 0, 0)));
    const glm::vec3 t2 = glm::cross(up, t1);
    const float e = 0.25f * step;
    auto broad_at = [&](const glm::vec3 &offset) {
        glm::dvec3 s = center + glm::dvec3(offset);
        glm::vec3 u = up;
        place_on_surface(s, u);
        return broad_gradient(s, u, step);
    };
    // Central differences: their error does not depend on the tangent basis
    // (forward ones made a step where it switches, on a sphere at y = 0.9).
    return glm::outerProduct((broad_at(t1 * e) - broad_at(-t1 * e)) / (2.0f * e), t1) +
           glm::outerProduct((broad_at(t2 * e) - broad_at(-t2 * e)) / (2.0f * e), t2);
}

namespace
{
std::atomic<uint64_t> s_nextGeneration{1};
}

JarTerrainSdf::JarTerrainSdf()
{
    settings_changed(); // unique per instance: a new one never reuses a freed one's cache
}

void JarTerrainSdf::settings_changed()
{
    _generation.store(s_nextGeneration.fetch_add(1), std::memory_order_relaxed);
}

void JarTerrainSdf::prepare()
{
    settings_changed(); // the noises may have been edited in place
}

// Gullies narrower than the caller's sample spacing would only alias (see
// DetailScope): coarse LODs leave them out. They change the height by well
// under that spacing, which the octree's margins allow for.
int JarTerrainSdf::erosion_octaves() const
{
    if (t_detailSize <= 0.0f)
        return _octaves;
    int octaves = 0;
    for (float width = _erosionScale; octaves < _octaves && width >= t_detailSize; width /= _lacunarity)
        ++octaves;
    return octaves;
}

JarTerrainSdf::Sample JarTerrainSdf::evaluate(const glm::dvec3 &surface, const glm::vec3 &up, bool localGradient) const
{
    if (analytic())
    {
        // Height, local and low-passed slope from one evaluation of each noise.
        const float step = 0.25f * _erosionScale;
        const Layers layers = analytic_layers(surface, 2.0f * step);
        const glm::vec3 local = layers.gradient - up * glm::dot(layers.gradient, up);
        const glm::vec3 broad = layers.broad - up * glm::dot(layers.broad, up);
        Sample result{layers.height, localGradient ? local : glm::vec3(0.0f), layers.height, glm::vec3(0.0f)};
        result.local = local;
        result.fine = local - broad;
        if (!_erosion || _octaves <= 0 || _erosionStrength <= 0.0f || max_layered_height() <= 0.0f)
        {
            result.broad = local;
            return result;
        }
        // Without gullies (coarse samples) the Jacobian is unused: it costs up
        // to 32 noise samples where samples are far apart.
        const glm::mat3 jacobian = erosion_octaves() > 0 ? broad_jacobian(surface, step) : glm::mat3(0.0f);
        return erode(surface, up, broad, jacobian, result);
    }
    const float h = layered_height(surface);
    Sample result{h, localGradient ? height_gradient(surface, up, h, 0.5f) : glm::vec3(0.0f), h, glm::vec3(0.0f)};
    const float range = max_layered_height();
    if (!_erosion || _octaves <= 0 || _erosionStrength <= 0.0f || range <= 0.0f)
    {
        result.broad = localGradient ? result.gradient : height_gradient(surface, up, h, 0.5f);
        return result;
    }
    // The gully direction field uses the low-passed slope: stripes are measured
    // up to a cell from their pivot, so any fast rotation of the direction (from
    // fine noise detail) would be magnified into steep, rippled slopes.
    const float step = 0.25f * _erosionScale;
    const glm::vec3 broad = broad_gradient(surface, up, step);
    const glm::mat3 broadJacobian = erosion_octaves() > 0 ? broad_jacobian(surface, step) : glm::mat3(0.0f);
    return erode(surface, up, broad, broadJacobian, result);
}

// Adds the erosion to `result` (layered height and gradient filled in), with
// the given low-passed slope of the layers.
JarTerrainSdf::Sample JarTerrainSdf::erode(const glm::dvec3 &surface, const glm::vec3 &up, const glm::vec3 &gradient,
                                           const glm::mat3 &gradientJacobian, Sample result) const
{
    const float h = result.layered;
    const float range = max_layered_height();
    result.broad = gradient;

    const float slope = glm::length(gradient);
    // Gully directions follow the slope, but pretend the input has a fixed
    // steepness (the eroded output is about equally steep everywhere).
    glm::vec3 gullySlope = slope > 1e-6f ? gradient / slope * _assumedSlope : glm::vec3(0.0f);
    // Fade target: -1 in valleys, +1 on peaks (here from altitude).
    float fadeTarget = std::clamp(h / range, -1.0f, 1.0f);
    // No gullies where the input is flat (peaks and valleys). The slope's
    // length is rounded off near zero: along a ridge or valley line of the
    // input it has a V kink, which the mask turned into a sharp crease.
    const float flatSlope = 0.1f * _assumedSlope;
    const float roundSlope = std::sqrt(slope * slope + flatSlope * flatSlope);
    const float firstMask = (roundSlope - flatSlope) / _assumedSlope;
    float mask = ease_out(firstMask);
    // Tangent gradients of the fade target and the mask, carried through the
    // octaves (chain rule) so the output slope is the eroded height's real
    // slope. distance() divides by it: with only the gullies' own slope, the
    // fades' slopes were missing and steep fades (cliffs, creases) gave
    // distances up to 4x too large, so the octree dropped surface (holes).
    glm::vec3 fadeGradient = std::abs(h) < range ? gradient / range : glm::vec3(0.0f);
    const glm::vec3 slopeGradient = slope > 1e-6f ? glm::transpose(gradientJacobian) * (gradient / slope) : glm::vec3(0.0f);
    glm::vec3 maskGradient = firstMask < 1.0f
                                 ? (2.0f * (1.0f - firstMask) * slope / (roundSlope * _assumedSlope)) * slopeGradient
                                 : glm::vec3(0.0f);

    // Gullies are scaled down by gully_weight and the strength scaled up by the
    // inverse, which keeps the faded (peak/valley) part strong: pointy peaks.
    // erosion_strength is the slope the gullies add: a stripe of width w and
    // amplitude a has slope a * 2pi / w.
    float amplitude = _erosionStrength * _erosionScale / Tau / _gullyWeight;
    float cellSize = _erosionScale;
    const bool sphere = _shape == SHAPE_SPHERE;

    // Jacobian of the gully direction field: that of the input slope, scaled
    // to the assumed steepness, then changed by the steering below.
    glm::mat3 gullySlopeJacobian =
        slope > 1e-6f ? (glm::mat3(1.0f) - glm::outerProduct(gradient, gradient) / (slope * slope)) * gradientJacobian *
                            (_assumedSlope / slope)
                      : glm::mat3(0.0f);
    // cross(up, v) as a matrix (up is treated as constant: it turns over the
    // planet radius, far slower than anything here).
    const glm::mat3 crossUp(glm::vec3(0.0f, up.z, -up.y), glm::vec3(-up.z, 0.0f, up.x), glm::vec3(up.y, -up.x, 0.0f));
    // Pivot offsets move with the sample: in all three axes on a sphere, in
    // the ground plane when flat (pivots there follow the sample height).
    const glm::vec3 offsetAxes = sphere ? glm::vec3(1.0f) : glm::vec3(1.0f, 0.0f, 1.0f);

    const int octaves = erosion_octaves();
    for (int octave = 0; octave < octaves; ++octave)
    {
        const float len = glm::length(gullySlope);
        float gully = fadeTarget, sine = 0.0f;
        glm::vec3 gullyGradient = fadeGradient, sineGradient(0.0f);
        glm::vec3 across(0.0f);
        glm::mat3 acrossJacobian(0.0f);
        if (len > 1e-6f)
        {
            // Stripes run along the slope; their phase varies across it.
            const glm::vec3 side = glm::cross(up, gullySlope);
            const float sideLength = std::max(glm::length(side), 1e-9f);
            across = side / sideLength;
            acrossJacobian = (glm::mat3(1.0f) - glm::outerProduct(across, across)) * crossUp * gullySlopeJacobian /
                             sideLength;
            const glm::mat3 acrossJacobianT = glm::transpose(acrossJacobian);
            const glm::vec3 acrossSlope = offsetAxes * across / cellSize;
            // Blend the stripes of nearby cells, each with its own random pivot
            // so rotating the stripes never distorts far from the pivot.
            const glm::dvec3 g = surface / double(cellSize);
            const glm::ivec3 base = glm::ivec3(glm::floor(g));
            const glm::vec3 f(g - glm::floor(g)); // position in the cell, [0, 1), from doubles
            // Per pivot only sums: the gradients are linear in its offset d,
            // so they are put together once after the loop.
            float c = 0.0f, s = 0.0f, weights = 0.0f;
            glm::vec3 fcd(0.0f), fsd(0.0f), fd(0.0f); // sums of falloff^2 * (cos, sin, 1) * d
            glm::vec3 wsd(0.0f), wcd(0.0f);           // sums of w * (sin, cos) * d
            const int ySpan = sphere ? 1 : 0;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -ySpan; dy <= ySpan; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        // Cells whose box is a cell away cannot hold a pivot in
                        // reach: skip them before hashing (same result, faster).
                        const float bx = dx < 0 ? f.x : (dx > 0 ? 1.0f - f.x : 0.0f);
                        const float by = dy < 0 ? f.y : (dy > 0 ? 1.0f - f.y : 0.0f);
                        const float bz = dz < 0 ? f.z : (dz > 0 ? 1.0f - f.z : 0.0f);
                        if (bx * bx + by * by + bz * bz >= 1.0f)
                            continue;
                        const glm::ivec3 cell = base + glm::ivec3(dx, dy, dz);
                        // From within the cell: small numbers, exact.
                        glm::vec3 d = f - (glm::vec3(dx, dy, dz) + cell_offset(cell));
                        if (!sphere)
                            d.y = 0.0f; // pivots follow the sample height
                        // Radius 1 cell: the farthest pivot that can be this close lies in
                        // a neighbouring cell, so the 3x3(x3) loop never misses one
                        // (a larger radius pops pivots in at cell borders).
                        const float falloff = std::max(0.0f, 1.0f - glm::dot(d, d));
                        const float w = falloff * falloff * falloff;
                        if (w <= 0.0f)
                            continue;
                        float ps, pc;
                        fast_sincos(glm::dot(d, across) * Tau, ps, pc);
                        c += w * pc;
                        s += w * ps;
                        weights += w;
                        const float f2 = falloff * falloff;
                        fcd += (f2 * pc) * d;
                        fsd += (f2 * ps) * d;
                        fd += f2 * d;
                        wsd += (w * ps) * d;
                        wcd += (w * pc) * d;
                    }
            // Gradients (per meter): of the weights, -6 falloff^2 / cell * d
            // (in the axes the offset moves in), and of the phase, which turns
            // with the stripes as well as moving along them:
            // tau * (acrossSlope + acrossJacobian^T d).
            const float wScale = -6.0f / cellSize;
            glm::vec3 cGradient = wScale * (offsetAxes * fcd) - Tau * (s * acrossSlope + acrossJacobianT * wsd);
            glm::vec3 sGradient = wScale * (offsetAxes * fsd) + Tau * (c * acrossSlope + acrossJacobianT * wcd);
            const glm::vec3 weightsGradient = wScale * (offsetAxes * fd);
            {
                // The small constant keeps this continuous where few pivots are
                // near: the stripes then fade to nothing instead of snapping.
                const float total = weights + 0.05f;
                c /= total;
                s /= total;
                cGradient = (cGradient - c * weightsGradient) / total;
                sGradient = (sGradient - s * weightsGradient) / total;
                // Normalized gullies: blended stripes shrink where they disagree;
                // lengths above ~0.5 are scaled back to 1 (below stays, avoiding
                // spikes), by a smooth min(1, 2l): a kink there made creases
                // and jumps in the slope.
                const float l = std::sqrt(c * c + s * s);
                const float x = 2.0f * l, x4 = x * x * x * x;
                const float root = std::sqrt(std::sqrt(1.0f + x4)); // (1 + x^4)^(1/4)
                const float k = 2.0f / root;                        // min(1, 2l) / l, smoothed
                gully = c * k;
                sine = s * k;
                gullyGradient = k * cGradient;
                sineGradient = k * sGradient;
                if (l > 1e-6f)
                {
                    // dk/dl = -4 x^3 / (1 + x^4)^(5/4); times grad l = (c grad c + s grad s) / l.
                    const float kSlope = -4.0f * x * x * x / (root * (1.0f + x4));
                    const glm::vec3 lGradient = (c * cGradient + s * sGradient) / l;
                    gullyGradient += (c * kSlope) * lGradient;
                    sineGradient += (s * kSlope) * lGradient;
                }
            }
        }

        // Fade the gullies towards the fade target where the mask says so.
        const float faded = glm::mix(fadeTarget, gully * _gullyWeight, mask);
        result.height += faded * amplitude;
        const glm::vec3 fadedGradient = (1.0f - mask) * fadeGradient + mask * _gullyWeight * gullyGradient +
                                        (gully * _gullyWeight - fadeTarget) * maskGradient;
        result.gradient += fadedGradient * amplitude;
        result.octaveSlopes[octave] = fadedGradient * amplitude;
        result.octaveCount = octave + 1;

        // Straight gullies: steer the next octave by the sign of the gully slope,
        // as if gullies had straight sides (like a triangle wave). The sign is
        // smoothed near zero so the direction never flips abruptly: over a
        // quarter of the sine, but at least crease_softness (on the finer
        // octaves a quarter is under a meter, and the next octave's stripes
        // turned so sharply along every crest that the crests crumpled).
        if (len > 1e-6f)
        {
            const float steer = 4.0f * amplitude * _gullyWeight / cellSize;
            // A smooth clamp of sine / width to [-1, 1] (a hard one left a kink).
            const float width = std::max(0.25f, _creaseSoftness * Tau / cellSize);
            const float x = sine / width, root = std::sqrt(1.0f + x * x);
            const float step = -x / root * steer;
            const glm::vec3 stepGradient = (-steer / (width * root * root * root)) * sineGradient;
            gullySlope += across * step;
            gullySlopeJacobian += acrossJacobian * step + glm::outerProduct(across, stepGradient);
        }

        // Stacked fading: no smaller gullies right on this octave's ridges and
        // creases (where its slope, the sine, is zero). The fade is rounded off
        // over crease_softness: a kink there became a V-notch as steep as ~70
        // degrees at every octave, narrower than a voxel at the finer ones, which
        // the meshes could only show as jagged teeth. Meters become sine units by
        // how fast the sine actually changes here: stripes squeezed by turning
        // change it faster than their width says, which narrowed the rounding.
        const float sineRate = std::max(Tau / cellSize, glm::length(sineGradient));
        const float soft = _creaseSoftness * sineRate; // meters -> sine near its zero
        const float kept = pow_inv(mask, _detail);
        const float rounded = soft_abs(sine, soft);
        const float eased = ease_out(rounded);
        // Derivatives of pow_inv, ease_out and soft_abs (all on [0, 1]).
        const float keptSlope = _detail * std::pow(std::max(0.0f, 1.0f - mask), _detail - 1.0f);
        const float easedSlope = rounded < 1.0f ? 2.0f * (1.0f - rounded) : 0.0f;
        const float roundedSlope =
            soft > 0.0f ? sine / std::sqrt(sine * sine + soft * soft) / (std::sqrt(1.0f + soft * soft) - soft)
                        : (sine < 0.0f ? -1.0f : 1.0f);
        maskGradient = keptSlope * eased * maskGradient + kept * easedSlope * roundedSlope * sineGradient;
        mask = kept * eased;
        fadeTarget = faded;
        fadeGradient = fadedGradient;
        cellSize /= _lacunarity;
        amplitude *= _gain;
    }
    // Tangent slope only (the radial part is the distance itself).
    result.gradient -= up * glm::dot(result.gradient, up);
    for (int k = 0; k < result.octaveCount; ++k)
        result.octaveSlopes[k] -= up * glm::dot(result.octaveSlopes[k], up);
    return result;
}

// Largest possible height above or below the base surface: the layers plus
// the sum of all erosion octave amplitudes (a geometric series).
float JarTerrainSdf::max_deviation() const
{
    float erosion = 0.0f;
    if (_erosion && _erosionStrength > 0.0f)
    {
        const float first = _erosionStrength * _erosionScale / Tau / _gullyWeight;
        erosion = first * (_gain < 1.0f ? (1.0f - std::pow(_gain, float(_octaves))) / (1.0f - _gain) : float(_octaves));
    }
    return max_layered_height() + erosion;
}

// The slope a height difference is divided by. At the surface it is the exact
// slope; further off, gullies much narrower than the distance no longer
// matter (the nearest surface is beyond them), so each octave's slope fades
// with gap / its width. The fine gullies' slope changes quickly: kept at full
// strength far away it made distances there swing (and grow too large).
float JarTerrainSdf::normalization(const Sample &sample, float gap) const
{
    glm::vec3 g = sample.broad + sample.gradient;
    float width = _erosionScale;
    const float gap2 = gap * gap;
    // The layers' detail under the low-pass's cutoff (known with JarNoise):
    // the same, over the cutoff's width. Without it, slopes of fine noise
    // detail were missing and distances on them came out too large.
    const float fineWidth = 0.5f * _erosionScale;
    g += sample.fine / (1.0f + gap2 / (fineWidth * fineWidth));
    for (int k = 0; k < sample.octaveCount; ++k)
    {
        const float fade = 1.0f / (1.0f + gap2 / (width * width));
        g -= (1.0f - fade) * sample.octaveSlopes[k];
        width /= _lacunarity;
    }
    return std::sqrt(1.0f + glm::dot(g, g));
}

float JarTerrainSdf::distance(const glm::vec3 &pos) const
{
    glm::dvec3 surface;
    glm::vec3 up;
    float above;
    frame(pos, surface, up, above);
    // Far from the surface the terrain cannot matter: return a lower bound,
    // blended in over a band so the field stays continuous.
    const float reach = max_deviation();
    const float band = 2.0f * reach + 1.0f; // wide enough to keep the blend shallow
    const float far = std::abs(above) - reach;
    const float bound = above > 0.0f ? far : -far;
    if (far >= band)
        return bound;

    // Height difference divided by the slope is close to a true distance. The
    // slope is the erosion's (exact, it makes the steep gullies) plus the
    // layers' low-passed one: both come for free. Leaving out the layers'
    // slope overestimated distances on steep mountainsides so much that the
    // octree dropped nodes with surface in them (holes).
    const Sample terrain = evaluate(surface, up, false);
    const float d = (above - terrain.height) / normalization(terrain, above - terrain.height);
    if (far <= 0.0f)
        return d;
    const float t = far / band;
    return glm::mix(d, bound, t * t * (3.0f - 2.0f * t));
}

float JarTerrainSdf::sample_height(const Vector3 &position) const
{
    glm::dvec3 surface;
    glm::vec3 up;
    float above;
    frame(glm::vec3(position.x, position.y, position.z), surface, up, above);
    return evaluate(surface, up).height;
}

Bounds JarTerrainSdf::bounds() const
{
    if (_shape != SHAPE_SPHERE)
        return Bounds::infinite();
    const float reach = max_deviation();
    return Bounds(glm::vec3(-(_radius + reach)), glm::vec3(_radius + reach));
}

// Volumes in UP_MODE_AUTO take their up direction and sea level from here.
bool JarTerrainSdf::natural_up(UpField &out) const
{
    out.center = glm::vec3(0.0f);
    out.axis = glm::vec3(0.0f, 1.0f, 0.0f);
    if (_shape == SHAPE_SPHERE)
    {
        out.mode = UpField::RADIAL;
        out.sea_level = _radius + _seaLevel;
    }
    else
    {
        out.mode = UpField::AXIS;
        out.sea_level = _seaLevel;
    }
    return true;
}

void JarTerrainSdf::_validate_property(PropertyInfo &property) const
{
    const String name = property.name;
    bool hidden = false;
    if (name == "radius")
        hidden = _shape != SHAPE_SPHERE;
    else if (name == "mountain_height" || name == "mountain_threshold" || name == "mountain_blend" ||
             name == "mountain_mask")
        hidden = !_mountainNoise.is_valid();
    else if (name == "erosion_strength" || name == "erosion_scale" || name == "octaves" || name == "lacunarity" ||
             name == "gain" || name == "detail" || name == "gully_weight" || name == "assumed_slope" ||
             name == "crease_softness")
        hidden = !_erosion;
    if (hidden)
        property.usage = PROPERTY_USAGE_NO_EDITOR;
}

void JarTerrainSdf::_bind_methods()
{
    BIND_ENUM_CONSTANT(SHAPE_FLAT);
    BIND_ENUM_CONSTANT(SHAPE_SPHERE);
#define BIND_PROP(m_name, m_type, m_hint, m_hint_text) \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarTerrainSdf::set_##m_name); \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarTerrainSdf::get_##m_name); \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
    BIND_PROP(shape, Variant::INT, PROPERTY_HINT_ENUM, "Flat,Sphere");
    BIND_PROP(radius, Variant::FLOAT, PROPERTY_HINT_RANGE, "1,100000,1,or_greater,suffix:m");
    BIND_PROP(sea_level, Variant::FLOAT, PROPERTY_HINT_NONE, "suffix:m");
    ADD_GROUP("Base", "");
    BIND_PROP(noise, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "JarNoise,FastNoiseLite");
    BIND_PROP(height, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,2000,0.1,or_greater,suffix:m");
    BIND_PROP(depth, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,2000,0.1,or_greater,suffix:m");
    ADD_GROUP("Mountains", "");
    BIND_PROP(mountain_noise, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "JarNoise,FastNoiseLite");
    BIND_PROP(mountain_mask, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "JarNoise,FastNoiseLite");
    BIND_PROP(mountain_height, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,5000,0.1,or_greater,suffix:m");
    BIND_PROP(mountain_threshold, Variant::FLOAT, PROPERTY_HINT_RANGE, "-1,1,0.01");
    BIND_PROP(mountain_blend, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.01,2,0.01");
    ADD_GROUP("Erosion", "");
    BIND_PROP(erosion, Variant::BOOL, PROPERTY_HINT_NONE, "");
    BIND_PROP(erosion_strength, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01,or_greater");
    BIND_PROP(erosion_scale, Variant::FLOAT, PROPERTY_HINT_RANGE, "1,1000,0.1,or_greater,suffix:m");
    BIND_PROP(octaves, Variant::INT, PROPERTY_HINT_RANGE, "0,10,1");
    BIND_PROP(lacunarity, Variant::FLOAT, PROPERTY_HINT_RANGE, "1,4,0.01");
    BIND_PROP(gain, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
    BIND_PROP(detail, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.1,5,0.01");
    BIND_PROP(gully_weight, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.05,1,0.01");
    BIND_PROP(assumed_slope, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.05,3,0.01");
    BIND_PROP(crease_softness, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m");
#undef BIND_PROP
    ClassDB::bind_method(D_METHOD("sample_height", "position"), &JarTerrainSdf::sample_height);
}


// Gradient of distance(): the radial part is exact (1 / slope normalization),
// the tangential part is the eroded height's slope by forward differences:
// three terrain evaluations instead of the generic four distance samples. (The
// offset samples need their own low-passed slope: the gullies rotate with it,
// which makes up much of the eroded slope.)
glm::vec3 JarTerrainSdf::gradient(const glm::vec3 &pos) const
{
    glm::dvec3 surface;
    glm::vec3 up;
    float above;
    frame(pos, surface, up, above);
    const float reach = max_deviation();
    const float far = std::abs(above) - reach;
    if (far >= 2.0f * reach + 1.0f)
        return up; // the far lower bound grows straight up
    if (far > 0.0f)
        return JarSignedDistanceField::gradient(pos); // blend band: rarely sampled

    const bool sphere = _shape == SHAPE_SPHERE;
    if (analytic())
    {
        // The eroded height's slope is exact here (layers and gullies, chain
        // rule throughout): no differences, which at planet scale (16 km: a
        // float step of 2 mm) were mostly rounding noise. The normalization's
        // own change is weighted by the distance: differences over a wider
        // step do there.
        const Sample c = evaluate(surface, up, false);
        const glm::vec3 slope = c.gradient + c.local;
        const float n = normalization(c, above - c.height);
        const float d = (above - c.height) / n;
        glm::vec3 nSlope(0.0f);
        if (std::abs(d) > 1e-3f)
        {
            const glm::vec3 t1 = glm::normalize(std::abs(up.y) < 0.9f ? glm::cross(up, glm::vec3(0, 1, 0))
                                                                       : glm::cross(up, glm::vec3(1, 0, 0)));
            const glm::vec3 t2 = glm::cross(up, t1);
            constexpr float Step = 0.25f;
            auto norm = [&](const glm::vec3 &offset) {
                glm::dvec3 s = surface + glm::dvec3(offset);
                if (sphere)
                    s = glm::normalize(s) * double(_radius);
                const Sample t = evaluate(s, sphere ? glm::vec3(s / double(_radius)) : up, false);
                return normalization(t, above - t.height);
            };
            nSlope = t1 * ((norm(t1 * Step) - n) / Step) + t2 * ((norm(t2 * Step) - n) / Step);
        }
        // On a sphere, moving `pos` sideways moves its surface point less.
        const float scale = sphere ? _radius / std::max(glm::length(pos), 1e-3f) : 1.0f;
        return (up - scale * (slope + d * nSlope)) / n;
    }
    const Sample c = evaluate(surface, up, false);
    const glm::vec3 t1 = glm::normalize(std::abs(up.y) < 0.9f ? glm::cross(up, glm::vec3(0, 1, 0))
                                                               : glm::cross(up, glm::vec3(1, 0, 0)));
    const glm::vec3 t2 = glm::cross(up, t1);
    constexpr float Step = 0.03f;
    // distance() = (above - height) / n, with n from the slope: both vary.
    auto norm = [&](const Sample &t) { return normalization(t, above - t.height); };
    auto at = [&](const glm::vec3 &offset) {
        glm::dvec3 s = surface + glm::dvec3(offset);
        if (sphere)
            s = glm::normalize(s) * double(_radius);
        return evaluate(s, sphere ? glm::vec3(s / double(_radius)) : up, false);
    };
    const Sample a = at(t1 * Step), b = at(t2 * Step);
    const float n = norm(c);
    glm::vec3 slope = t1 * ((a.height - c.height) / Step) + t2 * ((b.height - c.height) / Step);
    glm::vec3 nSlope = t1 * ((norm(a) - n) / Step) + t2 * ((norm(b) - n) / Step);
    // On a sphere, moving `pos` sideways moves its surface point less.
    if (sphere)
    {
        const float scale = _radius / std::max(glm::length(pos), 1e-3f);
        slope *= scale;
        nSlope *= scale;
    }
    const float d = (above - c.height) / n;
    return (up - slope - d * nSlope) / n;
}
