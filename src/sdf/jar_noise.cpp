#include "jar_noise.h"
#include <algorithm>
#include <cmath>

namespace
{
// Gradient directions: the 12 cube edge midpoints, 4 repeated to make 16 (an
// index from 4 bits, no modulo).
constexpr float Gradients[16][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0},  {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1},
                                    {1, 0, -1}, {-1, 0, -1}, {0, 1, 1},  {0, -1, 1},  {0, 1, -1}, {0, -1, -1},
                                    {1, 1, 0},  {-1, 1, 0},  {0, -1, 1}, {0, -1, -1}};

inline uint32_t hash3(int32_t x, int32_t y, int32_t z, uint32_t seed)
{
    uint32_t h = seed ^ (uint32_t(x) * 0x8da6b343U) ^ (uint32_t(y) * 0xd8163841U) ^ (uint32_t(z) * 0xcb1ab31fU);
    h ^= h >> 16;
    h *= 0x7feb352dU;
    h ^= h >> 15;
    h *= 0x846ca68bU;
    h ^= h >> 16;
    return h;
}

// Brings the noise to about [-1, 1] (measured: the peak of the sum below is
// about 0.0130 with this kernel).
constexpr float SimplexScale = 76.0f;

inline uint32_t octave_seed(uint32_t seed, int octave)
{
    return seed + uint32_t(octave) * 0x9E3779B9U;
}

// |x| rounded off near zero: ridged crests stay sharp but never have an
// infinitely thin kink (its gradient stays continuous).
constexpr float RidgeSoftness = 0.03f;
// The mean of one ridged octave ((1 - |n|)^2, measured): what a left out one
// adds (FBM octaves average 0).
constexpr float RidgedOctaveMean = 0.523f;
constexpr float BroadRidgeSoftness = 5.0f;
} // namespace

float JarNoise::simplex(const glm::dvec3 &p, uint32_t seed, glm::vec3 &gradient)
{
    constexpr float G3 = 1.0f / 6.0f;
    // The cell in doubles, the offset into it (small) in floats.
    const double s = (p.x + p.y + p.z) * (1.0 / 3.0);
    const double fi = std::floor(p.x + s), fj = std::floor(p.y + s), fk = std::floor(p.z + s);
    const int i = int(fi), j = int(fj), k = int(fk);
    const glm::vec3 d0(p - (glm::dvec3(fi, fj, fk) - (fi + fj + fk) * (1.0 / 6.0)));
    // Which simplex of the skewed cube: the order of the coordinates.
    glm::ivec3 o1, o2;
    if (d0.x >= d0.y)
    {
        if (d0.y >= d0.z)
            o1 = {1, 0, 0}, o2 = {1, 1, 0};
        else if (d0.x >= d0.z)
            o1 = {1, 0, 0}, o2 = {1, 0, 1};
        else
            o1 = {0, 0, 1}, o2 = {1, 0, 1};
    }
    else
    {
        if (d0.y < d0.z)
            o1 = {0, 0, 1}, o2 = {0, 1, 1};
        else if (d0.x < d0.z)
            o1 = {0, 1, 0}, o2 = {0, 1, 1};
        else
            o1 = {0, 1, 0}, o2 = {1, 1, 0};
    }
    const glm::ivec3 corners[4] = {glm::ivec3(0), o1, o2, glm::ivec3(1)};
    float n = 0.0f;
    glm::vec3 g(0.0f);
    for (int c = 0; c < 4; ++c)
    {
        const glm::vec3 d = d0 - glm::vec3(corners[c]) + float(c) * G3;
        // Kernel radius^2 0.5: zero before the next simplex starts (continuous).
        const float k2 = 0.5f - glm::dot(d, d);
        if (k2 <= 0.0f)
            continue;
        const float *gc = Gradients[hash3(i + corners[c].x, j + corners[c].y, k + corners[c].z, seed) & 15];
        const glm::vec3 grad(gc[0], gc[1], gc[2]);
        const float dot = glm::dot(grad, d);
        const float k4 = k2 * k2 * k2 * k2;
        n += k4 * dot;
        // d/dp (k2^4 * dot) = k2^4 * grad - 8 k2^3 * dot * d
        g += k4 * grad - (8.0f * k2 * k2 * k2 * dot) * d;
    }
    gradient = g * SimplexScale;
    return n * SimplexScale;
}

JarNoise::Sample JarNoise::sample(const glm::dvec3 &position, float broadWavelength, float minWavelength) const
{
    glm::dvec3 p = position;
    // Domain warp: p' = p + w * W(p * f). Gradients come back through the
    // warp's Jacobian: dn/dp = (I + J_w)^T dn/dp'.
    glm::mat3 warpJacobianT(1.0f);
    const bool warped = _warpStrength > 0.0f;
    if (warped)
    {
        glm::vec3 gx, gy, gz;
        const glm::dvec3 q = position * double(_warpFrequency);
        const uint32_t s = uint32_t(_seed) ^ 0x5bd1e995U;
        const glm::vec3 w(simplex(q, s, gx), simplex(q, s + 1, gy), simplex(q, s + 2, gz));
        p = position + double(_warpStrength) * glm::dvec3(w);
        const float k = _warpStrength * _warpFrequency;
        // Rows of J_w are the gradients of each component: (I + J)^T has them as columns.
        warpJacobianT = glm::mat3(1.0f) + k * glm::mat3(gx, gy, gz);
    }

    const int octaves = _fractal == FRACTAL_NONE ? 1 : std::max(1, _octaves);
    float frequency = _frequency, amplitude = 1.0f, norm = 0.0f, sum = 0.0f;
    glm::vec3 gradient(0.0f), broad(0.0f);
    for (int o = 0; o < octaves; ++o)
    {
        if (minWavelength > 0.0f && frequency * minWavelength > 1.0f)
        {
            // Too fine for this sample's spacing: its mean, no slope.
            sum += amplitude * (_fractal == FRACTAL_RIDGED ? RidgedOctaveMean : 0.0f);
            norm += amplitude;
            frequency *= _lacunarity;
            amplitude *= _gain;
            continue;
        }
        glm::vec3 g;
        float n = simplex(p * double(frequency), octave_seed(uint32_t(_seed), o), g);
        g *= frequency;
        // The low-pass: each octave weighted as central differences over half
        // the wavelength would see it (sin x / x), none past their first zero.
        const float x = 3.14159265f * broadWavelength * frequency;
        const float weight = x <= 0.0f ? 0.0f : (x < 3.14159265f ? std::sin(x) / x : 0.0f);
        const bool keep = weight > 0.0f;
        if (_fractal == FRACTAL_RIDGED)
        {
            // r = 1 - |n| (rounded), value r^2: sharp crests along n = 0.
            const float a = std::sqrt(n * n + RidgeSoftness * RidgeSoftness);
            const float r = std::max(0.0f, 1.0f - a);
            if (keep)
            {
                // A crest is far narrower than its octave: the low-pass also
                // rounds it off, over a few cutoffs (|n| changes by about 2.5
                // per wavelength, so this is about 2 cutoffs). The terrain's
                // gullies turn with this slope and are measured up to a cell
                // away: a crest turning it within meters made spiky ripples.
                const float soft = std::max(RidgeSoftness, BroadRidgeSoftness * broadWavelength * frequency);
                const float ab = std::sqrt(n * n + soft * soft);
                broad += weight * amplitude * (-2.0f * std::max(0.0f, 1.0f - ab) * (n / ab)) * g;
            }
            g *= -2.0f * r * (n / a);
            n = r * r;
        }
        else if (keep)
            broad += weight * amplitude * g;
        sum += amplitude * n;
        gradient += amplitude * g;
        norm += amplitude;
        frequency *= _lacunarity;
        amplitude *= _gain;
    }
    Sample result;
    const float inv = norm > 0.0f ? 1.0f / norm : 0.0f;
    result.value = sum * inv;
    result.gradient = gradient * inv;
    result.broad = broad * inv;
    if (_fractal == FRACTAL_RIDGED)
    {
        // [0, 1] -> [-1, 1], like the others.
        result.value = result.value * 2.0f - 1.0f;
        result.gradient *= 2.0f;
        result.broad *= 2.0f;
    }
    if (warped)
    {
        result.gradient = warpJacobianT * result.gradient;
        result.broad = warpJacobianT * result.broad;
    }
    return result;
}

Vector3 JarNoise::get_gradient_3d(const Vector3 &p) const
{
    const glm::vec3 g = sample(glm::dvec3(p.x, p.y, p.z)).gradient;
    return Vector3(g.x, g.y, g.z);
}

#define JAR_NOISE_SETTER(m_name, m_field, m_expr)                                                                      \
    void JarNoise::set_##m_name(decltype(m_field) value)                                                               \
    {                                                                                                                  \
        m_field = m_expr;                                                                                              \
        emit_changed();                                                                                                \
    }
JAR_NOISE_SETTER(seed, _seed, value)
JAR_NOISE_SETTER(frequency, _frequency, std::max(1e-7f, value))
JAR_NOISE_SETTER(fractal_type, _fractal, std::clamp(value, 0, 2))
JAR_NOISE_SETTER(octaves, _octaves, std::clamp(value, 1, 12))
JAR_NOISE_SETTER(lacunarity, _lacunarity, std::max(1.0f, value))
JAR_NOISE_SETTER(gain, _gain, std::clamp(value, 0.0f, 1.0f))
JAR_NOISE_SETTER(warp_strength, _warpStrength, std::max(0.0f, value))
JAR_NOISE_SETTER(warp_frequency, _warpFrequency, std::max(1e-7f, value))
#undef JAR_NOISE_SETTER

void JarNoise::_bind_methods()
{
    BIND_ENUM_CONSTANT(FRACTAL_NONE);
    BIND_ENUM_CONSTANT(FRACTAL_FBM);
    BIND_ENUM_CONSTANT(FRACTAL_RIDGED);
#define BIND_NOISE(m_name, m_type, m_hint, m_hint_text)                                                                \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarNoise::get_##m_name);                                           \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarNoise::set_##m_name);                                  \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
    BIND_NOISE(seed, Variant::INT, PROPERTY_HINT_NONE, "");
    BIND_NOISE(frequency, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.0000001,1,0.0000001,or_greater");
    ADD_GROUP("Fractal", "");
    BIND_NOISE(fractal_type, Variant::INT, PROPERTY_HINT_ENUM, "None,FBM,Ridged");
    BIND_NOISE(octaves, Variant::INT, PROPERTY_HINT_RANGE, "1,12,1");
    BIND_NOISE(lacunarity, Variant::FLOAT, PROPERTY_HINT_RANGE, "1,4,0.01");
    BIND_NOISE(gain, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
    ADD_GROUP("Domain Warp", "warp_");
    BIND_NOISE(warp_strength, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,10000,0.1,or_greater");
    BIND_NOISE(warp_frequency, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.0000001,1,0.0000001,or_greater");
#undef BIND_NOISE
    ClassDB::bind_method(D_METHOD("get_noise_3d", "x", "y", "z"), &JarNoise::get_noise_3d);
    ClassDB::bind_method(D_METHOD("get_noise_2d", "x", "y"), &JarNoise::get_noise_2d);
    ClassDB::bind_method(D_METHOD("get_gradient_3d", "position"), &JarNoise::get_gradient_3d);
}
