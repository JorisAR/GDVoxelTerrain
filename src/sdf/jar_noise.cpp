#include "jar_noise.h"
#include "simd/simplex.h"
#include <algorithm>
#include <cmath>

namespace
{
// Gradient directions: the 12 cube edge midpoints, 4 repeated to make 16 (an
// index from 4 bits, no modulo).
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
        const double qx[3] = {q.x, q.x, q.x}, qy[3] = {q.y, q.y, q.y}, qz[3] = {q.z, q.z, q.z};
        const uint32_t seeds[3] = {s, s + 1, s + 2};
        float wv[3], wx[3], wy[3], wz[3];
        jar_simd::simplex()(qx, qy, qz, seeds, 3, wv, wx, wy, wz);
        const glm::vec3 w(wv[0], wv[1], wv[2]);
        gx = glm::vec3(wx[0], wy[0], wz[0]);
        gy = glm::vec3(wx[1], wy[1], wz[1]);
        gz = glm::vec3(wx[2], wy[2], wz[2]);
        p = position + double(_warpStrength) * glm::dvec3(w);
        const float k = _warpStrength * _warpFrequency;
        // Rows of J_w are the gradients of each component: (I + J)^T has them as columns.
        warpJacobianT = glm::mat3(1.0f) + k * glm::mat3(gx, gy, gz);
    }

    const int octaves = _fractal == FRACTAL_NONE ? 1 : std::max(1, _octaves);
    float frequency = _frequency, amplitude = 1.0f, norm = 0.0f, sum = 0.0f;
    glm::vec3 gradient(0.0f), broad(0.0f);
    // Octaves in groups, ahead of the loop below, which then only reads them.
    constexpr int Group = 8;
    const jar_simd::SimplexLanes kernel = jar_simd::simplex();
    float groupValue[Group], groupX[Group], groupY[Group], groupZ[Group];
    int groupSize = 0, groupNext = 0;
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
        if (groupNext == groupSize)
        {
            double px[Group], py[Group], pz[Group];
            uint32_t seeds[Group];
            groupSize = groupNext = 0;
            float f = frequency;
            for (int q = o; q < octaves && groupSize < Group; ++q, f *= _lacunarity)
            {
                if (minWavelength > 0.0f && f * minWavelength > 1.0f)
                    continue;
                px[groupSize] = p.x * double(f);
                py[groupSize] = p.y * double(f);
                pz[groupSize] = p.z * double(f);
                seeds[groupSize] = octave_seed(uint32_t(_seed), q);
                ++groupSize;
            }
            kernel(px, py, pz, seeds, groupSize, groupValue, groupX, groupY, groupZ);
        }
        float n = groupValue[groupNext];
        glm::vec3 g(groupX[groupNext], groupY[groupNext], groupZ[groupNext]);
        ++groupNext;
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

bool JarNoise::simd_active()
{
    return jar_simd::simd_active();
}

void JarNoise::use_simd(bool enabled)
{
    jar_simd::use_simd(enabled);
}

void JarNoise::_bind_methods()
{
    ClassDB::bind_static_method("JarNoise", D_METHOD("simd_active"), &JarNoise::simd_active);
    ClassDB::bind_static_method("JarNoise", D_METHOD("use_simd", "enabled"), &JarNoise::use_simd);
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
