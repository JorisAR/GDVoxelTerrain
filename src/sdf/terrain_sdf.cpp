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

void JarTerrainSdf::frame(const glm::vec3 &pos, glm::vec3 &surface, glm::vec3 &up, float &above) const
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

float JarTerrainSdf::layered_height(const glm::vec3 &surface) const
{
    const bool sphere = _shape == SHAPE_SPHERE;
    float h = 0.0f;
    if (_noise.is_valid())
    {
        const float n = sphere ? _noise->get_noise_3d(surface.x, surface.y, surface.z)
                               : _noise->get_noise_2d(surface.x, surface.z);
        h = n > 0.0f ? n * _height : n * _depth;
    }
    if (_mountainNoise.is_valid() && _mountainHeight > 0.0f)
    {
        const float m = sphere ? _mountainNoise->get_noise_3d(surface.x, surface.y, surface.z)
                               : _mountainNoise->get_noise_2d(surface.x, surface.z);
        const float t = std::clamp((m - _mountainThreshold) / _mountainBlend, 0.0f, 1.0f);
        h += _mountainHeight * t * t * (3.0f - 2.0f * t);
    }
    return h;
}

float JarTerrainSdf::max_layered_height() const
{
    return std::max(std::abs(_height), std::abs(_depth)) + (_mountainNoise.is_valid() ? _mountainHeight : 0.0f);
}

glm::vec3 JarTerrainSdf::height_gradient(const glm::vec3 &surface, const glm::vec3 &up, float h, float step) const
{
    // Forward differences along two tangent directions: the local slope.
    const glm::vec3 t1 = glm::normalize(std::abs(up.y) < 0.9f ? glm::cross(up, glm::vec3(0, 1, 0))
                                                               : glm::cross(up, glm::vec3(1, 0, 0)));
    const glm::vec3 t2 = glm::cross(up, t1);
    const float e = step;
    auto at = [&](const glm::vec3 &offset) {
        glm::vec3 p = surface + offset;
        if (_shape == SHAPE_SPHERE)
            p = glm::normalize(p) * _radius;
        return layered_height(p);
    };
    return t1 * ((at(t1 * e) - h) / e) + t2 * ((at(t2 * e) - h) / e);
}

// Low-passed slope of the layers: tetrahedral differences `step` apart in
// fixed world directions, projected on the tangent plane. Unlike differences
// along a tangent frame (which must switch axes somewhere on a sphere), this
// is continuous everywhere: the frame switch left a ~2 m step in the eroded
// terrain along two circles of latitude.
glm::vec3 JarTerrainSdf::broad_gradient(const glm::vec3 &surface, const glm::vec3 &up, float step) const
{
    const float d = step / std::sqrt(3.0f);
    static const glm::vec3 corners[4] = {glm::vec3(1, -1, -1), glm::vec3(-1, -1, 1), glm::vec3(-1, 1, -1),
                                         glm::vec3(1, 1, 1)};
    glm::vec3 sum(0.0f);
    for (const glm::vec3 &c : corners)
    {
        glm::vec3 p = surface + c * d;
        if (_shape == SHAPE_SPHERE)
            p = glm::normalize(p) * _radius;
        else
            p.y = 0.0f;
        sum += c * layered_height(p);
    }
    const glm::vec3 g = sum / (4.0f * d);
    return g - up * glm::dot(g, up);
}

JarTerrainSdf::Sample JarTerrainSdf::evaluate(const glm::vec3 &surface, const glm::vec3 &up, bool localGradient) const
{
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
    return erode(surface, up, broad_gradient(surface, up, 0.25f * _erosionScale), result);
}

// Adds the erosion to `result` (layered height and gradient filled in), with
// the given low-passed slope of the layers.
JarTerrainSdf::Sample JarTerrainSdf::erode(const glm::vec3 &surface, const glm::vec3 &up, const glm::vec3 &gradient,
                                           Sample result) const
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
            const glm::vec3 f = g - glm::vec3(base); // position in the cell, [0, 1)
            float c = 0.0f, s = 0.0f, weights = 0.0f;
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
                        float ps, pc;
                        fast_sincos(glm::dot(d, across) * Tau, ps, pc);
                        c += w * pc;
                        s += w * ps;
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

float JarTerrainSdf::distance(const glm::vec3 &pos) const
{
    glm::vec3 surface, up;
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
    const glm::vec3 slope = terrain.gradient + terrain.broad;
    const float d = (above - terrain.height) / std::sqrt(1.0f + glm::dot(slope, slope));
    if (far <= 0.0f)
        return d;
    const float t = far / band;
    return glm::mix(d, bound, t * t * (3.0f - 2.0f * t));
}

float JarTerrainSdf::sample_height(const Vector3 &position) const
{
    glm::vec3 surface, up;
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
    else if (name == "mountain_height" || name == "mountain_threshold" || name == "mountain_blend")
        hidden = _mountainNoise.is_null();
    else if (name == "erosion_strength" || name == "erosion_scale" || name == "octaves" || name == "lacunarity" ||
             name == "gain" || name == "detail" || name == "gully_weight" || name == "assumed_slope")
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
    BIND_PROP(noise, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite");
    BIND_PROP(height, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,2000,0.1,or_greater,suffix:m");
    BIND_PROP(depth, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,2000,0.1,or_greater,suffix:m");
    ADD_GROUP("Mountains", "");
    BIND_PROP(mountain_noise, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite");
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
    glm::vec3 surface, up;
    float above;
    frame(pos, surface, up, above);
    const float reach = max_deviation();
    const float far = std::abs(above) - reach;
    if (far >= 2.0f * reach + 1.0f)
        return up; // the far lower bound grows straight up
    if (far > 0.0f)
        return JarSignedDistanceField::gradient(pos); // blend band: rarely sampled

    const Sample c = evaluate(surface, up, false);
    const bool sphere = _shape == SHAPE_SPHERE;
    const glm::vec3 t1 = glm::normalize(std::abs(up.y) < 0.9f ? glm::cross(up, glm::vec3(0, 1, 0))
                                                               : glm::cross(up, glm::vec3(1, 0, 0)));
    const glm::vec3 t2 = glm::cross(up, t1);
    constexpr float Step = 0.03f;
    // distance() = (above - height) / n, with n from the slope: both vary.
    auto norm = [](const Sample &t) {
        const glm::vec3 g = t.gradient + t.broad;
        return std::sqrt(1.0f + glm::dot(g, g));
    };
    auto at = [&](const glm::vec3 &offset) {
        glm::vec3 s = surface + offset;
        if (sphere)
            s = glm::normalize(s) * _radius;
        return evaluate(s, sphere ? s / _radius : up, false);
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
