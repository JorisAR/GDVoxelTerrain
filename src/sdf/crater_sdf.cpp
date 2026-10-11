#include "crater_sdf.h"
#include <cmath>

namespace
{
uint32_t hash4(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    uint32_t h = a * 0x8da6b343u ^ b * 0xd8163841u ^ c * 0xcb1ab31fu ^ d * 0x165667b1u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    return h ^ (h >> 16);
}

float unit(uint32_t &h)
{
    h = hash4(h, 0x9e3779b9u, 1, 2);
    return float(h >> 8) * (1.0f / 16777216.0f);
}

// Height profile of a crater of radius 1 at distance x from its center: a
// parabolic bowl (-depth at the center, 0 at the rim), plus a rim that peaks at
// the edge and falls off outside.
float profile(float x, float depth, float rim)
{
    const float bowl = x < 1.0f ? depth * (x * x - 1.0f) : 0.0f;
    const float r = (x - 1.0f) / (x < 1.0f ? 0.25f : 0.45f);
    return bowl + rim * std::exp(-r * r);
}
} // namespace

float JarCraterSdf::crater_height(const glm::vec3 &pos, const UpField &up) const
{
    // Point on the reference surface (sphere or plane) and its up direction.
    const glm::vec3 u = up.up(pos);
    const glm::vec3 s = up.mode == UpField::RADIAL ? up.center + u * up.sea_level
                                                   : pos - u * glm::dot(pos - up.center, u);
    float height = 0.0f;
    float radius = _size;
    for (int octave = 0; octave < _octaves; ++octave, radius *= 0.5f)
    {
        // One candidate per cell of 4 radii; only cells around the surface
        // count, so the ground sees one layer of cells, flat or round.
        const float cell = radius * 4.0f;
        const glm::ivec3 base = glm::ivec3(glm::floor(s / cell));
        for (int z = -1; z <= 1; ++z)
            for (int y = -1; y <= 1; ++y)
                for (int x = -1; x <= 1; ++x)
                {
                    const glm::ivec3 c = base + glm::ivec3(x, y, z);
                    uint32_t h = hash4(uint32_t(c.x), uint32_t(c.y), uint32_t(c.z), uint32_t(_seed * 8 + octave));
                    if (unit(h) > _density)
                        continue;
                    glm::vec3 center = (glm::vec3(c) + glm::vec3(unit(h), unit(h), unit(h))) * cell;
                    const glm::vec3 delta = center - s;
                    const float along = glm::dot(delta, u);
                    if (std::abs(along) > cell * 0.5f)
                        continue;
                    // Distance along the ground; radii vary per crater.
                    const float r = radius * (0.5f + 0.5f * unit(h));
                    const float d = glm::length(delta - u * along) / r;
                    if (d < 2.5f)
                        height += profile(d, _depth, _rim) * r;
                }
    }
    return height;
}

float JarCraterSdf::distance(const glm::vec3 &pos) const
{
    const float d = child_distance(pos);
    // Far from the surface craters cannot matter (they change height by at most
    // depth * size): skip the cell search.
    if (std::abs(d) > _size * (_depth + _rim) * 2.0f + 1.0f || _sdf.is_null())
        return d;
    UpField up;
    if (!_sdf->natural_up(up))
        up = UpField(); // +Y
    return d - crater_height(pos, up);
}

Bounds JarCraterSdf::bounds() const
{
    const Bounds b = child_bounds();
    if (!b.is_valid() || b.is_infinite())
        return b;
    return b.expanded(_size * _rim * 2.0f);
}

void JarCraterSdf::_bind_methods()
{
#define BIND_PROP(m_name, m_type, m_hint, m_hint_text)                                                                  \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarCraterSdf::set_##m_name);                              \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarCraterSdf::get_##m_name);                                       \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
    BIND_PROP(size, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.1,1000,0.1,or_greater,suffix:m");
    BIND_PROP(octaves, Variant::INT, PROPERTY_HINT_RANGE, "1,8,1");
    BIND_PROP(density, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
    BIND_PROP(depth, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
    BIND_PROP(rim, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
    BIND_PROP(seed, Variant::INT, PROPERTY_HINT_NONE, "");
#undef BIND_PROP
}
