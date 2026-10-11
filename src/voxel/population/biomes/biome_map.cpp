#include "biome_map.h"
#include <cmath>

glm::vec2 JarBiomeMap::climate(const glm::vec3 &position, const UpField &up) const
{
    float t = temperature_noise.is_valid() ? temperature_noise->get_noise_3d(position.x, position.y, position.z) : 0.0f;
    const float m = moisture_noise.is_valid() ? moisture_noise->get_noise_3d(position.x, position.y, position.z) : 0.0f;
    // Colder higher up (only above sea level), and towards the poles of a planet.
    t -= temperature_lapse * std::max(0.0f, up.height(position)) / 100.0f;
    if (up.mode == UpField::RADIAL)
    {
        const glm::vec3 axis(pole_axis.x, pole_axis.y, pole_axis.z);
        const float latitude = glm::dot(up.up(position), axis);
        t -= latitude_effect * latitude * latitude;
    }
    return glm::vec2(t, m);
}

BiomeWeights JarBiomeMap::evaluate(const glm::vec3 &position, const UpField &up) const
{
    BiomeWeights result;
    const int n = int(biomes.size());
    if (n <= 1)
        return result; // biome 0 everywhere (or none)
    const glm::vec2 c = climate(position, up);
    // Gaussian falloff by distance in climate space; keep the strongest few.
    float best[BiomeWeights::Slots] = {-1.0f, -1.0f, -1.0f, -1.0f};
    int ids[BiomeWeights::Slots] = {0, 0, 0, 0};
    for (int i = 0; i < n; ++i)
    {
        Ref<JarBiome> b = biomes[i];
        if (b.is_null())
            continue;
        const glm::vec2 d = c - glm::vec2(b->get_temperature(), b->get_moisture());
        const float w = std::exp(-glm::dot(d, d) / (blend * blend));
        for (int s = 0; s < BiomeWeights::Slots; ++s)
            if (w > best[s])
            {
                for (int k = BiomeWeights::Slots - 1; k > s; --k)
                {
                    best[k] = best[k - 1];
                    ids[k] = ids[k - 1];
                }
                best[s] = w;
                ids[s] = i;
                break;
            }
    }
    float total = 0.0f;
    result.count = 0;
    for (int s = 0; s < BiomeWeights::Slots; ++s)
        if (best[s] > 0.0f)
        {
            result.ids[result.count] = ids[s];
            result.weights[result.count] = best[s];
            total += best[s];
            result.count++;
        }
    if (total <= 0.0f)
    {
        result = BiomeWeights();
        return result;
    }
    for (int s = 0; s < result.count; ++s)
        result.weights[s] /= total;
    return result;
}

void JarBiomeMap::_bind_methods()
{
#define BIND_MAP(m_name, m_type, m_hint, m_hint_text)                                                                  \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarBiomeMap::get_##m_name);                                        \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarBiomeMap::set_##m_name);                               \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
    BIND_MAP(biomes, Variant::ARRAY, PROPERTY_HINT_TYPE_STRING,
             String::num(Variant::OBJECT) + "/" + String::num(PROPERTY_HINT_RESOURCE_TYPE) + ":JarBiome");
    ADD_GROUP("Climate", "");
    BIND_MAP(temperature_noise, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite");
    BIND_MAP(moisture_noise, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite");
    BIND_MAP(temperature_lapse, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,2,0.01");
    BIND_MAP(latitude_effect, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,2,0.01");
    BIND_MAP(pole_axis, Variant::VECTOR3, PROPERTY_HINT_NONE, "");
    BIND_MAP(blend, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.01,1,0.01");
#undef BIND_MAP
}
