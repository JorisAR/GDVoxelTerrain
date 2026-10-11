#ifndef TERRAIN_SDF_H
#define TERRAIN_SDF_H

#include "signed_distance_field.h"
#include <godot_cpp/classes/fast_noise_lite.hpp>

class JarTerrainSdf : public JarSignedDistanceField
{
    GDCLASS(JarTerrainSdf, JarSignedDistanceField);

  private:
    Ref<FastNoiseLite> _noiseLite;
    float _heightScale = 256.0f;
    const float Epsilon = 0.01f;
    const float InvEps = 1.0f / Epsilon;

  public:
    JarTerrainSdf()
    {
    }

    void set_noise(Ref<FastNoiseLite> noise)
    {
        set_and_notify(_noiseLite, noise);
    }

    Ref<FastNoiseLite> get_noise() const
    {
        return _noiseLite;
    }

    void set_height_scale(float heightScale)
    {
        set_and_notify(_heightScale, heightScale);
    }

    float get_height_scale() const
    {
        return _heightScale;
    }

  protected:
    float sample_height(const Vector2 &pos) const
    {
        if (_noiseLite.is_null())
            return 0.0f;
        float noise = _noiseLite->get_noise_2d(pos.x, pos.y);
        return _heightScale * (noise > 0 ? 2.0f * noise : 1.0f * noise);
    }

    Vector2 sample_gradient(const Vector2 &pos, float height) const
    {
        float heightX = sample_height(pos + Vector2(Epsilon, 0));
        float heightZ = sample_height(pos + Vector2(0, Epsilon));

        float gradientX = (heightX - height) * InvEps;
        float gradientZ = (heightZ - height) * InvEps;

        return Vector2(gradientX, gradientZ);
    }

    virtual float distance(const glm::vec3 &pos) const override
    {
        Vector2 samplePos(pos.x, pos.z);
        float height = sample_height(samplePos);
        Vector2 gradient = sample_gradient(samplePos, height);

        // Adjust SDF based on gradient
        return (pos.y - height) / sqrt(1 + gradient.x * gradient.x + gradient.y * gradient.y);
    }

    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_noise", "noise"), &JarTerrainSdf::set_noise);
        ClassDB::bind_method(D_METHOD("get_noise"), &JarTerrainSdf::get_noise);
        ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "noise", PROPERTY_HINT_RESOURCE_TYPE, "FastNoiseLite"), "set_noise",
                     "get_noise");

        ClassDB::bind_method(D_METHOD("set_height_scale", "height_scale"), &JarTerrainSdf::set_height_scale);
        ClassDB::bind_method(D_METHOD("get_height_scale"), &JarTerrainSdf::get_height_scale);
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_scale"), "set_height_scale", "get_height_scale");
    }
};

#endif // TERRAIN_SDF_H
