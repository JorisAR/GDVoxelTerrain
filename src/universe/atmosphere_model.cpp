#include "atmosphere_model.h"
#include <algorithm>
#include <cmath>
#include <godot_cpp/variant/packed_byte_array.hpp>

namespace
{
constexpr double Pi = 3.14159265358979323846;

double texel_to_unit(double u, int n)
{
    return (u - 0.5 / n) / (1.0 - 1.0 / n);
}

double unit_to_texel(double x, int n)
{
    return 0.5 / n + x * (1.0 - 1.0 / n);
}

double smoothstep(double a, double b, double x)
{
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double rayleigh_phase(double nu)
{
    return 3.0 / (16.0 * Pi) * (1.0 + nu * nu);
}

// Cornette-Shanks.
double mie_phase(double nu, double g)
{
    const double g2 = g * g;
    return 3.0 / (8.0 * Pi) * ((1.0 - g2) * (1.0 + nu * nu)) /
           ((2.0 + g2) * std::pow(std::max(1.0 + g2 - 2.0 * g * nu, 1e-6), 1.5));
}

// Integral of an exponential profile with scale height s from 0 to h.
double profile_integral(double s, double h)
{
    return s * (1.0 - std::exp(-h / s));
}

glm::dvec3 exp3(const glm::dvec3 &v)
{
    return glm::dvec3(std::exp(v.x), std::exp(v.y), std::exp(v.z));
}

// Integral over a step of a source term S under extinction e: (S - S e^-e dt) / e.
glm::dvec3 step_integral(const glm::dvec3 &source, const glm::dvec3 &extinction, double dt)
{
    glm::dvec3 result;
    for (int c = 0; c < 3; ++c)
        result[c] = extinction[c] > 1e-12 ? source[c] * (1.0 - std::exp(-extinction[c] * dt)) / extinction[c]
                                          : source[c] * dt;
    return result;
}
} // namespace

bool AtmosphereParams::operator==(const AtmosphereParams &o) const
{
    const auto close = [](double a, double b) { return std::abs(a - b) <= 1e-6 * std::max(std::abs(a), std::abs(b)); };
    return close(bottom, o.bottom) && close(top, o.top) && rayleighDepth == o.rayleighDepth &&
           rayleighHeight == o.rayleighHeight && mieDepth == o.mieDepth && mieHeight == o.mieHeight &&
           mieAnisotropy == o.mieAnisotropy && mieAbsorption == o.mieAbsorption && ozoneDepth == o.ozoneDepth &&
           ozoneCenter == o.ozoneCenter && ozoneWidth == o.ozoneWidth && groundAlbedo == o.groundAlbedo &&
           sunsetStrength == o.sunsetStrength;
}

AtmosphereModel::Medium AtmosphereModel::medium(double h) const
{
    const double thickness = _p.top - _p.bottom;
    h = std::max(h, 0.0);
    const double rayleigh = std::exp(-h / (_p.rayleighHeight * thickness));
    const double mie = std::exp(-h / (_p.mieHeight * thickness));
    const double halfWidth = 0.5 * _p.ozoneWidth * thickness;
    const double ozone = std::max(0.0, 1.0 - std::abs(h - _p.ozoneCenter * thickness) / halfWidth);
    // Everything fades to nothing over the top tenth: the atmosphere never
    // ends in a visible edge, whatever its profile.
    const double top = std::clamp((thickness - h) / (0.1 * thickness), 0.0, 1.0);
    const double fade = top * top * (3.0 - 2.0 * top);
    Medium m;
    m.rayleighScattering = _rayleigh * (rayleigh * fade);
    m.mieScattering = glm::dvec3(_mie * mie * fade);
    m.extinction = m.rayleighScattering + m.mieScattering * (1.0 + _p.mieAbsorption) + _ozone * (ozone * fade);
    return m;
}

void AtmosphereModel::transmittance_uv(double r, double mu, double &u, double &v) const
{
    const double bottom2 = _p.bottom * _p.bottom, top2 = _p.top * _p.top;
    const double H = std::sqrt(top2 - bottom2);
    const double rho = std::sqrt(std::max(r * r - bottom2, 0.0));
    const double d = std::max(0.0, -r * mu + std::sqrt(std::max(r * r * (mu * mu - 1.0) + top2, 0.0)));
    const double dMin = _p.top - r, dMax = rho + H;
    u = unit_to_texel(dMax > dMin ? (d - dMin) / (dMax - dMin) : 0.0, TransmittanceWidth);
    v = unit_to_texel(rho / H, TransmittanceHeight);
}

glm::dvec3 AtmosphereModel::lookup(const std::vector<glm::dvec3> &table, int width, int height, double u,
                                   double v) const
{
    const double x = std::clamp(u * width - 0.5, 0.0, double(width - 1));
    const double y = std::clamp(v * height - 0.5, 0.0, double(height - 1));
    const int x0 = int(x), y0 = int(y);
    const int x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
    const double fx = x - x0, fy = y - y0;
    const glm::dvec3 a = glm::mix(table[y0 * width + x0], table[y0 * width + x1], fx);
    const glm::dvec3 b = glm::mix(table[y1 * width + x0], table[y1 * width + x1], fx);
    return glm::mix(a, b, fy);
}

glm::dvec3 AtmosphereModel::transmittance(double r, double mu) const
{
    double u, v;
    transmittance_uv(std::clamp(r, _p.bottom, _p.top), mu, u, v);
    return lookup(_transmittance, TransmittanceWidth, TransmittanceHeight, u, v);
}

glm::dvec3 AtmosphereModel::sunlight(double r, double muSun) const
{
    r = std::clamp(r, _p.bottom, _p.top);
    // Behind the planet: none (softened over a few tenths of a degree).
    const double sinHorizon = _p.bottom / r;
    const double muHorizon = -std::sqrt(std::max(0.0, 1.0 - sinHorizon * sinHorizon));
    return transmittance(r, muSun) * smoothstep(muHorizon - 0.01, muHorizon + 0.01, muSun);
}

glm::dvec3 AtmosphereModel::multiple_scattering(double r, double muSun) const
{
    const double u = unit_to_texel(std::clamp(muSun * 0.5 + 0.5, 0.0, 1.0), MultiScatteringSize);
    const double v = unit_to_texel(std::clamp((r - _p.bottom) / (_p.top - _p.bottom), 0.0, 1.0), MultiScatteringSize);
    return lookup(_multiScattering, MultiScatteringSize, MultiScatteringSize, u, v);
}

glm::dvec3 AtmosphereModel::ray_radiance(double r, double mu, double muSun, double nu, int steps) const
{
    const double bottom2 = _p.bottom * _p.bottom, top2 = _p.top * _p.top;
    const double groundDisc = r * r * (mu * mu - 1.0) + bottom2;
    const bool ground = mu < 0.0 && groundDisc >= 0.0;
    const double length = ground ? -r * mu - std::sqrt(groundDisc)
                                 : -r * mu + std::sqrt(std::max(r * r * (mu * mu - 1.0) + top2, 0.0));
    if (length <= 0.0)
        return glm::dvec3(0.0);
    const double dt = length / steps;
    const double phaseR = rayleigh_phase(nu), phaseM = mie_phase(nu, _p.mieAnisotropy);
    glm::dvec3 radiance(0.0), throughput(1.0);
    for (int i = 0; i < steps; ++i)
    {
        const double t = (i + 0.5) * dt;
        const double rt = std::sqrt(r * r + t * t + 2.0 * r * mu * t);
        const double muSunT = (r * muSun + t * nu) / rt;
        const Medium m = medium(rt - _p.bottom);
        const glm::dvec3 sun = sunlight(rt, muSunT);
        const glm::dvec3 source = m.rayleighScattering * (phaseR * sun) + m.mieScattering * (phaseM * sun) +
                                  (m.rayleighScattering + m.mieScattering) * multiple_scattering(rt, muSunT);
        radiance += throughput * step_integral(source, m.extinction, dt);
        throughput *= exp3(-m.extinction * dt);
    }
    return radiance;
}

glm::dvec3 AtmosphereModel::sky_ambient(double r, double muSun) const
{
    if (!is_built())
        return glm::dvec3(0.0);
    r = std::clamp(r, _p.bottom + 1e-3, _p.top);
    const glm::dvec3 sun(std::sqrt(std::max(0.0, 1.0 - muSun * muSun)), muSun, 0.0);
    // Cosine-weighted directions over the upper hemisphere (golden angle spiral).
    constexpr int Count = 24;
    glm::dvec3 sum(0.0);
    for (int k = 0; k < Count; ++k)
    {
        const double xi = (k + 0.5) / Count;
        const double sinTheta = std::sqrt(xi), cosTheta = std::sqrt(1.0 - xi);
        const double phi = k * 2.39996322972865332;
        const glm::dvec3 dir(sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi));
        sum += ray_radiance(r, cosTheta, muSun, glm::dot(dir, sun), 10);
    }
    return sum / double(Count);
}

void AtmosphereModel::build(const AtmosphereParams &params)
{
    _p = params;
    const double thickness = _p.top - _p.bottom;
    // Per meter at sea level, so the vertical optical depths come out as given.
    _rayleigh = _p.rayleighDepth / profile_integral(_p.rayleighHeight * thickness, thickness);
    _mie = _p.mieDepth / profile_integral(_p.mieHeight * thickness, thickness);
    _ozone = _p.ozoneDepth / std::max(0.5 * _p.ozoneWidth * thickness, 1e-6);

    // Transmittance to space.
    const double bottom2 = _p.bottom * _p.bottom, top2 = _p.top * _p.top;
    const double H = std::sqrt(top2 - bottom2);
    _transmittance.assign(TransmittanceWidth * TransmittanceHeight, glm::dvec3(1.0));
    for (int j = 0; j < TransmittanceHeight; ++j)
        for (int i = 0; i < TransmittanceWidth; ++i)
        {
            const double xMu = texel_to_unit((i + 0.5) / TransmittanceWidth, TransmittanceWidth);
            const double xR = texel_to_unit((j + 0.5) / TransmittanceHeight, TransmittanceHeight);
            const double rho = H * xR;
            const double r = std::sqrt(rho * rho + bottom2);
            const double dMin = _p.top - r, dMax = rho + H;
            const double d = dMin + xMu * (dMax - dMin);
            const double mu = d <= 0.0 ? 1.0 : std::clamp((H * H - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
            constexpr int Steps = 40;
            const double dt = d / Steps;
            glm::dvec3 depth(0.0);
            for (int s = 0; s < Steps; ++s)
            {
                const double t = (s + 0.5) * dt;
                const double rt = std::sqrt(r * r + t * t + 2.0 * r * mu * t);
                depth += medium(rt - _p.bottom).extinction * dt;
            }
            // Only ever used for sunlight on its way in: see sunsetStrength.
            _transmittance[j * TransmittanceWidth + i] = exp3(-depth * _p.sunsetStrength);
        }

    // Multiple scattering (Hillaire 2020): second order scattering over the
    // sphere of directions, as a geometric series of all orders.
    _multiScattering.assign(MultiScatteringSize * MultiScatteringSize, glm::dvec3(0.0));
    constexpr int Directions = 64;
    constexpr int Steps = 20;
    const double isotropic = 1.0 / (4.0 * Pi);
    for (int j = 0; j < MultiScatteringSize; ++j)
        for (int i = 0; i < MultiScatteringSize; ++i)
        {
            const double muSun = (i + 0.5) / MultiScatteringSize * 2.0 - 1.0;
            const double r = _p.bottom + std::max((j + 0.5) / MultiScatteringSize, 0.002) * thickness;
            const glm::dvec3 sun(std::sqrt(std::max(0.0, 1.0 - muSun * muSun)), muSun, 0.0);
            glm::dvec3 secondOrder(0.0), transfer(0.0);
            for (int k = 0; k < Directions; ++k)
            {
                // Uniform over the sphere (golden angle spiral).
                const double y = 1.0 - 2.0 * (k + 0.5) / Directions;
                const double ring = std::sqrt(std::max(0.0, 1.0 - y * y));
                const double phi = k * 2.39996322972865332;
                const glm::dvec3 dir(ring * std::cos(phi), y, ring * std::sin(phi));
                const double mu = y, nu = glm::dot(dir, sun);
                const double groundDisc = r * r * (mu * mu - 1.0) + bottom2;
                const bool ground = mu < 0.0 && groundDisc >= 0.0;
                const double length = ground ? -r * mu - std::sqrt(groundDisc)
                                             : -r * mu + std::sqrt(std::max(r * r * (mu * mu - 1.0) + top2, 0.0));
                const double dt = std::max(length, 0.0) / Steps;
                glm::dvec3 throughput(1.0), light(0.0), f(0.0);
                for (int s = 0; s < Steps; ++s)
                {
                    const double t = (s + 0.5) * dt;
                    const double rt = std::sqrt(r * r + t * t + 2.0 * r * mu * t);
                    const double muSunT = (r * muSun + t * nu) / rt;
                    const Medium m = medium(rt - _p.bottom);
                    const glm::dvec3 scattering = m.rayleighScattering + m.mieScattering;
                    light += throughput * step_integral(scattering * sunlight(rt, muSunT) * isotropic, m.extinction, dt);
                    f += throughput * step_integral(scattering, m.extinction, dt);
                    throughput *= exp3(-m.extinction * dt);
                }
                if (ground)
                {
                    // Sunlight bounced off the ground.
                    const double muSunGround = (r * muSun + length * nu) / _p.bottom;
                    light += throughput * sunlight(_p.bottom, muSunGround) * std::max(muSunGround, 0.0) *
                             _p.groundAlbedo / Pi;
                }
                secondOrder += light;
                transfer += f;
            }
            secondOrder /= double(Directions);
            transfer /= double(Directions);
            glm::dvec3 psi;
            for (int c = 0; c < 3; ++c)
                psi[c] = secondOrder[c] / std::max(1.0 - transfer[c] * isotropic * 4.0 * Pi, 1e-3);
            _multiScattering[j * MultiScatteringSize + i] = psi;
        }
}

namespace
{
Ref<Image> make_image(const std::vector<glm::dvec3> &table, int width, int height)
{
    PackedByteArray bytes;
    bytes.resize(int64_t(width) * height * 3 * sizeof(float));
    float *out = reinterpret_cast<float *>(bytes.ptrw());
    for (size_t i = 0; i < table.size(); ++i)
        for (int c = 0; c < 3; ++c)
            out[i * 3 + c] = float(table[i][c]);
    return Image::create_from_data(width, height, false, Image::FORMAT_RGBF, bytes);
}
} // namespace

Ref<Image> AtmosphereModel::transmittance_image() const
{
    return make_image(_transmittance, TransmittanceWidth, TransmittanceHeight);
}

Ref<Image> AtmosphereModel::multiple_scattering_image() const
{
    return make_image(_multiScattering, MultiScatteringSize, MultiScatteringSize);
}
