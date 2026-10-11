#ifndef JAR_TERRAIN_MATERIAL_H
#define JAR_TERRAIN_MATERIAL_H

#include <algorithm>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/color.hpp>

using namespace godot;

// One surface material of a volume (grass, rock, sand, snow, painted stone...).
// Its index in JarVoxelVolume.materials is the id edits paint and the ground
// rules refer to. The volume packs all materials into a palette and texture
// arrays for the terrain shader, so any shader written against that contract
// (the defaults, or your own) sees the same materials.
class JarTerrainMaterial : public Resource
{
    GDCLASS(JarTerrainMaterial, Resource);

  private:
    Color albedo = Color(0.5f, 0.5f, 0.5f);
    float roughness = 0.85f;
    Ref<Texture2D> albedo_texture;
    Ref<Texture2D> normal_texture;
    float texture_size = 4.0f; // meters covered by one texture tile
    float variation = 0.25f;   // procedural brightness / hue variation

    void changed() { emit_changed(); }

  public:
    Color get_albedo() const { return albedo; }
    void set_albedo(const Color &v) { albedo = v; changed(); }
    float get_roughness() const { return roughness; }
    void set_roughness(float v) { roughness = std::clamp(v, 0.0f, 1.0f); changed(); }
    Ref<Texture2D> get_albedo_texture() const { return albedo_texture; }
    void set_albedo_texture(const Ref<Texture2D> &v) { albedo_texture = v; changed(); notify_property_list_changed(); }
    Ref<Texture2D> get_normal_texture() const { return normal_texture; }
    void set_normal_texture(const Ref<Texture2D> &v) { normal_texture = v; changed(); notify_property_list_changed(); }
    float get_texture_size() const { return texture_size; }
    void set_texture_size(float v) { texture_size = std::max(0.01f, v); changed(); }
    float get_variation() const { return variation; }
    void set_variation(float v) { variation = std::clamp(v, 0.0f, 1.0f); changed(); }

  protected:
    void _validate_property(PropertyInfo &property) const
    {
        if (property.name == StringName("texture_size") && albedo_texture.is_null() && normal_texture.is_null())
            property.usage = PROPERTY_USAGE_NO_EDITOR;
    }

    static void _bind_methods()
    {
#define BIND_MAT(m_name, m_type, m_hint, m_hint_text)                                                                  \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarTerrainMaterial::get_##m_name);                                 \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarTerrainMaterial::set_##m_name);                        \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
        BIND_MAT(albedo, Variant::COLOR, PROPERTY_HINT_COLOR_NO_ALPHA, "");
        BIND_MAT(roughness, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
        BIND_MAT(variation, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,1,0.01");
        ADD_GROUP("Textures", "");
        BIND_MAT(albedo_texture, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Texture2D");
        BIND_MAT(normal_texture, Variant::OBJECT, PROPERTY_HINT_RESOURCE_TYPE, "Texture2D");
        BIND_MAT(texture_size, Variant::FLOAT, PROPERTY_HINT_RANGE, "0.1,100,0.1,or_greater,suffix:m");
#undef BIND_MAT
    }
};

#endif // JAR_TERRAIN_MATERIAL_H
