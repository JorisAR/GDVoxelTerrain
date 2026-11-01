#ifndef JAR_NOISE_H
#define JAR_NOISE_H

#include <cstdint>
#include <glm/glm.hpp>
#include <godot_cpp/classes/resource.hpp>

using namespace godot;

// Fractal noise made for terrain: 3D simplex noise that gives its exact
// gradient along with the value (no extra samples for slopes), with FBM or
// ridged fractals and an optional domain warp. It runs inside the extension
// (no engine call per sample), and one evaluation can also return a "broad"
// gradient: a low-pass of the slope (as the terrain's erosion needs), as
// central differences over half a given wavelength would see it.
//
// Values are in about [-1, 1]. Gradients are per unit of input (per meter
// for positions in meters), already including the frequency.
class JarNoise : public Resource
{
    GDCLASS(JarNoise, Resource);

  public:
    enum FractalType
    {
        FRACTAL_NONE = 0,   // one octave
        FRACTAL_FBM = 1,    // octaves summed: rolling, natural detail
        FRACTAL_RIDGED = 2, // 1 - |n| per octave, squared: sharp crests, branching ridges
    };

    struct Sample
    {
        float value = 0.0f;
        glm::vec3 gradient{0.0f};
        glm::vec3 broad{0.0f}; // low-passed gradient: octaves well over `broadWavelength` long
    };

  private:
    int _seed = 0;
    float _frequency = 0.01f;
    int _fractal = FRACTAL_FBM;
    int _octaves = 5;
    float _lacunarity = 2.0f;
    float _gain = 0.5f;
    float _warpStrength = 0.0f; // input units the domain is pushed around by (0: off)
    float _warpFrequency = 0.005f;

  protected:
    static void _bind_methods();

  public:
    // One octave of simplex noise at `p` (unit frequency) with its gradient.
    // The input is in doubles: its lattice cell is found in doubles and the
    // rest done in floats from within the cell, so the noise keeps its
    // precision far from the origin (a float step is 6 cm at 600 km, which
    // turned planet surfaces into rounding noise).
    static float simplex(const glm::dvec3 &p, uint32_t seed, glm::vec3 &gradient);

    // The fractal noise at `p`. `broadWavelength` > 0 also fills Sample::broad.
    Sample sample(const glm::dvec3 &p, float broadWavelength = 0.0f) const;

    float get_noise_3d(float x, float y, float z) const { return sample(glm::dvec3(x, y, z)).value; }
    float get_noise_2d(float x, float y) const { return sample(glm::dvec3(x, 0.0, y)).value; }
    Vector3 get_gradient_3d(const Vector3 &p) const;

    int get_seed() const { return _seed; }
    void set_seed(int value);
    float get_frequency() const { return _frequency; }
    void set_frequency(float value);
    int get_fractal_type() const { return _fractal; }
    void set_fractal_type(int value);
    int get_octaves() const { return _octaves; }
    void set_octaves(int value);
    float get_lacunarity() const { return _lacunarity; }
    void set_lacunarity(float value);
    float get_gain() const { return _gain; }
    void set_gain(float value);
    float get_warp_strength() const { return _warpStrength; }
    void set_warp_strength(float value);
    float get_warp_frequency() const { return _warpFrequency; }
    void set_warp_frequency(float value);
};

VARIANT_ENUM_CAST(JarNoise::FractalType);

#endif // JAR_NOISE_H
