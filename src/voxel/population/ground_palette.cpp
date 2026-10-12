#include "ground_palette.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d.hpp>

namespace
{
// Average color of a texture (linear), by repeated 2x2 box filtering.
glm::vec3 texture_average(const Ref<Texture2D> &texture)
{
    if (texture.is_null())
        return glm::vec3(1.0f);
    Ref<Image> image = texture->get_image();
    if (image.is_null() || image->is_empty())
        return glm::vec3(1.0f);
    image = image->duplicate();
    if (image->is_compressed() && image->decompress() != OK)
        return glm::vec3(1.0f);
    image->convert(Image::FORMAT_RGBA8);
    while (image->get_width() > 8 || image->get_height() > 8)
        image->resize(std::max(1, image->get_width() / 2), std::max(1, image->get_height() / 2),
                      Image::INTERPOLATE_BILINEAR);
    glm::vec3 sum(0.0f);
    const int count = image->get_width() * image->get_height();
    for (int y = 0; y < image->get_height(); ++y)
        for (int x = 0; x < image->get_width(); ++x)
        {
            const Color c = image->get_pixel(x, y).srgb_to_linear();
            sum += glm::vec3(c.r, c.g, c.b);
        }
    return sum / float(std::max(count, 1));
}
} // namespace

std::shared_ptr<const GroundPalette> GroundPalette::build(const TypedArray<JarTerrainMaterial> &materials,
                                                          const Ref<JarBiomeMap> &biomes,
                                                          const Ref<Material> &terrainMaterial)
{
    auto palette = std::make_shared<GroundPalette>();
    std::vector<glm::vec3> colors;
    for (int64_t i = 0; i < materials.size(); ++i)
    {
        const Ref<JarTerrainMaterial> m = materials[i];
        glm::vec3 c = palette->_fallback;
        if (m.is_valid())
        {
            const Color albedo = m->get_albedo().srgb_to_linear();
            c = glm::vec3(albedo.r, albedo.g, albedo.b) * texture_average(m->get_albedo_texture());
        }
        colors.push_back(c);
    }
    auto color_of = [&](int id, const glm::vec3 &otherwise) {
        return id >= 0 && id < int(colors.size()) ? colors[id] : otherwise;
    };

    // The terrain shader's own ground rule material (its default is 0).
    int groundRule = 0;
    if (const Ref<ShaderMaterial> shader = terrainMaterial; shader.is_valid())
    {
        const Variant v = shader->get_shader_parameter("ground_material");
        if (v.get_type() == Variant::INT || v.get_type() == Variant::FLOAT)
            groundRule = int(v);
    }
    palette->_fallback = color_of(groundRule, palette->_fallback);

    palette->_map = biomes;
    if (biomes.is_valid())
    {
        const TypedArray<JarBiome> list = biomes->get_biomes();
        for (int64_t b = 0; b < list.size(); ++b)
        {
            const Ref<JarBiome> biome = list[b];
            // The terrain shaders give the first 4 biomes their own ground.
            const int id = biome.is_valid() && b < 4 ? biome->get_ground_material() : -1;
            palette->_biomes.push_back(color_of(id, palette->_fallback));
        }
    }
    return palette;
}

glm::vec3 GroundPalette::color_at(const glm::vec3 &position, const UpField &up) const
{
    if (_map.is_null() || _biomes.empty())
        return _fallback;
    const BiomeWeights w = _map->evaluate(position, up);
    glm::vec3 sum(0.0f);
    float total = 0.0f;
    for (int i = 0; i < w.count; ++i)
    {
        const int id = w.ids[i];
        sum += w.weights[i] * (id >= 0 && id < int(_biomes.size()) ? _biomes[id] : _fallback);
        total += w.weights[i];
    }
    return total > 0.0f ? sum / total : _fallback;
}
