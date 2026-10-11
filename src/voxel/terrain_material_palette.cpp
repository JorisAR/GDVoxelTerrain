#include "terrain_material_palette.h"
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>
#include <unordered_map>

namespace
{
// Collects textures into an array, deduplicated; returns the layer of each.
struct LayerBuilder
{
    TypedArray<Image> images;
    std::unordered_map<uint64_t, int> layers; // texture instance id -> layer
    Vector2i size;

    int add(const Ref<Texture2D> &texture)
    {
        if (texture.is_null())
            return -1;
        auto it = layers.find(texture->get_instance_id());
        if (it != layers.end())
            return it->second;
        Ref<Image> image = texture->get_image();
        if (image.is_null() || image->is_empty())
            return -1;
        image = image->duplicate();
        if (image->is_compressed())
            image->decompress();
        image->convert(Image::FORMAT_RGBA8);
        if (images.is_empty())
            size = image->get_size();
        else if (image->get_size() != size)
            image->resize(size.x, size.y, Image::INTERPOLATE_BILINEAR);
        image->generate_mipmaps();
        const int layer = int(images.size());
        images.push_back(image);
        layers.emplace(texture->get_instance_id(), layer);
        return layer;
    }

    Ref<Texture2DArray> build() const
    {
        if (images.is_empty())
            return Ref<Texture2DArray>();
        Ref<Texture2DArray> array;
        array.instantiate();
        array->create_from_images(images);
        return array;
    }
};
} // namespace

TypedArray<JarTerrainMaterial> TerrainMaterialPalette::default_materials()
{
    struct Entry
    {
        Color albedo;
        float roughness, variation;
    };
    static const Entry Entries[] = {
        {Color(0.30f, 0.42f, 0.18f), 0.90f, 0.35f}, // grass
        {Color(0.46f, 0.44f, 0.41f), 0.80f, 0.30f}, // rock
        {Color(0.80f, 0.72f, 0.52f), 0.90f, 0.20f}, // sand
        {Color(0.34f, 0.32f, 0.24f), 0.90f, 0.30f}, // seabed
        {Color(0.92f, 0.94f, 0.97f), 0.55f, 0.08f}, // snow
        {Color(0.40f, 0.28f, 0.18f), 0.95f, 0.30f}, // dirt
        {Color(0.22f, 0.22f, 0.24f), 0.70f, 0.20f}, // dark stone
        {Color(0.75f, 0.45f, 0.22f), 0.60f, 0.30f}, // clay
    };
    TypedArray<JarTerrainMaterial> result;
    for (const Entry &e : Entries)
    {
        Ref<JarTerrainMaterial> m;
        m.instantiate();
        m->set_albedo(e.albedo);
        m->set_roughness(e.roughness);
        m->set_variation(e.variation);
        result.push_back(m);
    }
    return result;
}

void TerrainMaterialPalette::apply(const TypedArray<JarTerrainMaterial> &materials, const Ref<ShaderMaterial> &target)
{
    if (target.is_null())
        return;
    const int count = std::min<int>(int(materials.size()), 255);
    Ref<Image> palette = Image::create_empty(256, 2, false, Image::FORMAT_RGBAF);
    LayerBuilder albedo, normal;
    for (int i = 0; i < count; ++i)
    {
        Ref<JarTerrainMaterial> m = materials[i];
        if (m.is_null())
        {
            palette->set_pixel(i, 0, Color(1, 0, 1, 1)); // magenta: missing material
            palette->set_pixel(i, 1, Color(0.25f, -1, -1, 0));
            continue;
        }
        const Color c = m->get_albedo().srgb_to_linear();
        palette->set_pixel(i, 0, Color(c.r, c.g, c.b, m->get_roughness()));
        palette->set_pixel(i, 1, Color(1.0f / m->get_texture_size(), float(albedo.add(m->get_albedo_texture())),
                                       float(normal.add(m->get_normal_texture())), m->get_variation()));
    }
    target->set_shader_parameter("jar_palette", ImageTexture::create_from_image(palette));
    target->set_shader_parameter("jar_material_count", count);
    const Ref<Texture2DArray> albedoArray = albedo.build();
    const Ref<Texture2DArray> normalArray = normal.build();
    target->set_shader_parameter("jar_albedo_textures", albedoArray);
    target->set_shader_parameter("jar_normal_textures", normalArray);
    target->set_shader_parameter("jar_has_albedo_textures", albedoArray.is_valid());
    target->set_shader_parameter("jar_has_normal_textures", normalArray.is_valid());
}
