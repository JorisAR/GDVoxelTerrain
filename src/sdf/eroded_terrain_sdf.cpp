#include "eroded_terrain_sdf.h"
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

// Random point inside a grid cell, in [0, 1)^3.
glm::vec3 cell_offset(const glm::ivec3 &cell)
{
    uint32_t h = hash_u32(uint32_t(cell.x) * 0x8da6b343U ^ uint32_t(cell.y) * 0xd8163841U ^ uint32_t(cell.z) * 0xcb1ab31fU);
    const float a = float(h & 0x3FF) / 1024.0f;
    h = hash_u32(h);
    const float b = float(h & 0x3FF) / 1024.0f;
    h = hash_u32(h);
    const float c = float(h & 0x3FF) / 1024.0f;
    return glm::vec3(a, b, c);
}
} // namespace

void JarErodedTerrainSdf::frame(const glm::vec3 &pos, glm::vec3 &surface, glm::vec3 &up, float &above) const
{
    if (_shape == SHAPE_SPHERE)
    {
        const float len = glm::length(pos);
        up = len > 1e-6f ? pos / len : glm::vec3(0.0f, 1.0f, 0.0f);
        surface = up * _radius;
        above = len - _radius;
    }
    else
    {
        up = glm::vec3(0.0f, 1.0f, 0.0f);
        surface = glm::vec3(pos.x, 0.0f, pos.z);
        above = pos.y;
    }
}

float JarErodedTerrainSdf::base_height(const glm::vec3 &surface) const
{
    if (_noise.is_null())
        return 0.0f;
    const float n = _shape == SHAPE_SPHERE ? _noise->get_noise_3d(surface.x, surface.y, surface.z)
                                           : _noise->get_noise_2d(surface.x, surface.z);
    return n * _height;
}

glm::vec3 JarErodedTerrainSdf::base_gradient(const glm::vec3 &surface, const glm::vec3 &up, float h, float step) const
{
    // Differences along two tangent directions: forward ones for the local
    // slope, central ones with a wide step for a low-passed slope.
    const glm::vec3 t1 = glm::normalize(std::abs(up.y) < 0.9f ? glm::cross(up, glm::vec3(0, 1, 0))
                                                               : glm::cross(up, glm::vec3(1, 0, 0)));
    const glm::vec3 t2 = glm::cross(up, t1);
    const float e = step;
    auto at = [&](const glm::vec3 &offset) {
        glm::vec3 p = surface + offset;
        if (_shape == SHAPE_SPHERE)
            p = glm::normalize(p) * _radius;
        return base_height(p);
    };
    if (step <= 1.0f)
        return t1 * ((at(t1 * e) - h) / e) + t2 * ((at(t2 * e) - h) / e);
    return t1 * ((at(t1 * e) - at(-t1 * e)) / (2.0f * e)) + t2 * ((at(t2 * e) - at(-t2 * e)) / (2.0f * e));
}

JarErodedTerrainSdf::Sample JarErodedTerrainSdf::erode(const glm::vec3 &surface, const glm::vec3 &up, float h) const
{
    Sample result{h, base_gradient(surface, up, h, 0.5f)};
    if (_octaves <= 0 || _erosionStrength <= 0.0f || _height == 0.0f)
        return result;
    // The gully direction field uses the low-passed slope: stripes are measured
    // up to a cell from their pivot, so any fast rotation of the direction (from
    // fine noise detail) would be magnified into steep, rippled slopes.
    const glm::vec3 gradient = base_gradient(surface, up, h, 0.25f * _erosionScale);

    const float slope = glm::length(gradient);
    // Gully directions follow the slope, but pretend the input has a fixed
    // steepness (the eroded output is about equally steep everywhere).
    glm::vec3 gullySlope = slope > 1e-6f ? gradient / slope * _assumedSlope : glm::vec3(0.0f);
    // Fade target: -1 in valleys, +1 on peaks (here from altitude).
    float fadeTarget = std::clamp(h / std::abs(_height), -1.0f, 1.0f);
    // No gullies where the input is flat (peaks and valleys).
    float mask = ease_out(slope / _assumedSlope);

    // Gullies are scaled down by gully_weight and the strength scaled up by the
    // inverse, which keeps the faded (peak/valley) part strong: pointy peaks.
    // erosion_strength is the slope the gullies add: a stripe of width w and
    // amplitude a has slope a * 2pi / w.
    float amplitude = _erosionStrength * _erosionScale / Tau / _gullyWeight;
    float cellSize = _erosionScale;
    const bool sphere = _shape == SHAPE_SPHERE;

    for (int octave = 0; octave < _octaves; ++octave)
    {
        const float len = glm::length(gullySlope);
        float gully = fadeTarget, sine = 0.0f;
        glm::vec3 across(0.0f);
        if (len > 1e-6f)
        {
            // Stripes run along the slope; their phase varies across it.
            across = glm::normalize(glm::cross(up, gullySlope / len));
            // Blend the stripes of nearby cells, each with its own random pivot
            // so rotating the stripes never distorts far from the pivot.
            const glm::vec3 g = surface / cellSize;
            const glm::ivec3 base = glm::ivec3(glm::floor(g));
            float c = 0.0f, s = 0.0f, weights = 0.0f;
            const int ySpan = sphere ? 1 : 0;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -ySpan; dy <= ySpan; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const glm::ivec3 cell = base + glm::ivec3(dx, dy, dz);
                        glm::vec3 pivot = glm::vec3(cell) + cell_offset(cell);
                        if (!sphere)
                            pivot.y = g.y;
                        const glm::vec3 d = g - pivot;
                        // Radius 1 cell: the farthest pivot that can be this close lies in
                        // a neighbouring cell, so the 3x3(x3) loop never misses one
                        // (a larger radius pops pivots in at cell borders).
                        const float falloff = std::max(0.0f, 1.0f - glm::dot(d, d));
                        const float w = falloff * falloff * falloff;
                        if (w <= 0.0f)
                            continue;
                        const float phase = glm::dot(d, across) * Tau;
                        c += w * std::cos(phase);
                        s += w * std::sin(phase);
                        weights += w;
                    }
            {
                // The small constant keeps this continuous where few pivots are
                // near: the stripes then fade to nothing instead of snapping.
                c /= weights + 0.05f;
                s /= weights + 0.05f;
                // Normalized gullies: blended stripes shrink where they disagree;
                // scale lengths above 0.5 back to 1 (below stays, avoiding spikes).
                const float l = std::sqrt(c * c + s * s);
                const float k = l > 1e-6f ? std::min(1.0f, 2.0f * l) / l : 0.0f;
                gully = c * k;
                sine = s * k;
            }
        }

        // Fade the gullies towards the fade target where the mask says so.
        const float faded = glm::mix(fadeTarget, gully * _gullyWeight, mask);
        result.height += faded * amplitude;
        // Output slope: d/dx cos(phase) = -sin(phase) * Tau / cellSize.
        result.gradient += across * (-sine * Tau / cellSize * amplitude * _gullyWeight * mask);

        // Straight gullies: steer the next octave by the sign of the gully slope,
        // as if gullies had straight sides (like a triangle wave). The sign is
        // smoothed near zero so the direction never flips abruptly.
        if (len > 1e-6f)
            gullySlope += across * (-std::clamp(sine * 4.0f, -1.0f, 1.0f) * 4.0f * amplitude * _gullyWeight / cellSize);

        // Stacked fading: no smaller gullies right on this octave's ridges and
        // creases (where its slope, the sine, is zero).
        mask = pow_inv(mask, _detail) * ease_out(std::abs(sine));
        fadeTarget = faded;
        cellSize /= _lacunarity;
        amplitude *= _gain;
    }
    return result;
}

// Largest possible height above or below the base surface: base noise plus the
// sum of all octave amplitudes (a geometric series).
float JarErodedTerrainSdf::max_deviation() const
{
    const float first = _erosionStrength * _erosionScale / Tau / _gullyWeight;
    const float series = _gain < 1.0f ? (1.0f - std::pow(_gain, float(_octaves))) / (1.0f - _gain) : float(_octaves);
    return std::abs(_height) + first * series;
}

float JarErodedTerrainSdf::distance(const glm::vec3 &pos) const
{
    glm::vec3 surface, up;
    float above;
    frame(pos, surface, up, above);
    // Far from the surface the erosion cannot matter: return a lower bound,
    // blended in over a band so the field stays continuous.
    const float reach = max_deviation();
    const float band = 0.5f * reach + 1.0f;
    const float far = std::abs(above) - reach;
    const float bound = above > 0.0f ? far : -far;
    if (far >= band)
        return bound;

    const float h = base_height(surface);
    const Sample eroded = erode(surface, up, h);
    // Height difference divided by the slope: close to a true distance.
    const float d = (above - eroded.height) / std::sqrt(1.0f + glm::dot(eroded.gradient, eroded.gradient));
    if (far <= 0.0f)
        return d;
    const float t = far / band;
    return glm::mix(d, bound, t * t * (3.0f - 2.0f * t));
}

float JarErodedTerrainSdf::sample_height(const Vector3 &position) const
{
    glm::vec3 surface, up;
    float above;
    frame(glm::vec3(position.x, position.y, position.z), surface, up, above);
    const float h = base_height(surface);
    return erode(surface, up, h).height;
}

Bounds JarErodedTerrainSdf::bounds() const
{
    if (_shape != SHAPE_SPHERE)
        return Bounds::infinite();
    const float reach = max_deviation();
    return Bounds(glm::vec3(-(_radius + reach)), glm::vec3(_radius + reach));
}

void JarErodedTerrainSdf::_bind_methods()
{
    BIND_ENUM_CONSTANT(SHAPE_FLAT);
    BIND_ENUM_CONSTANT(SHAPE_SPHERE);
#define BIND_PROP(m_name, m_type, m_hint, m_hint_text)                                                                  \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarErodedTerrainSdf::set_##m_name);                       \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarErodedTerrainSdf::get_##m_name);                                \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
    BIND_PROP(shape, Variant::INT, PROPERTY_HINT_ENUM, "Flat,Sphere");
    BIND_PROP(radius, Variant::FLOAT, PROPERTY_HINT_RANGE, "1,100000,1,or_greater,suffix:m");
    BIND_PROP(noise, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite");
    BIND_PROP(height, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,2000,0.1,or_greater,suffix:m");
    ADD_GROUP("Erosion", "");
    BIND_PROP(erosion_strength, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01,or_greater");
    BIND_PROP(erosion_scale, Variant::FLOAT, PROPERTY_HINT_RANGE, "1,1000,0.1,or_greater,suffix:m");
    BIND_PROP(octaves, Variant::INT, PROPERTY_HINT_RANGE, "0,10,1");
    BIND_PROP(lacunarity, Variant::FLOAT, PROPERTY_HINT_RANGE, "1,4,0.01");
    BIND_PROP(gain, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
    BIND_PROP(detail, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.1,5,0.01");
    BIND_PROP(gully_weight, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.05,1,0.01");
    BIND_PROP(assumed_slope, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.05,3,0.01");
#undef BIND_PROP
    ClassDB::bind_method(D_METHOD("sample_height", "position"), &JarErodedTerrainSdf::sample_height);
}
