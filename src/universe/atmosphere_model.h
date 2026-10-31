#ifndef JAR_ATMOSPHERE_MODEL_H
#define JAR_ATMOSPHERE_MODEL_H

#include <glm/glm.hpp>
#include <godot_cpp/classes/image.hpp>
#include <vector>

using namespace godot;

// A planet's atmosphere as a smooth sphere (after Hillaire, "A Scalable and
// Production Ready Sky and Atmosphere Rendering Technique", 2020): Rayleigh
// scattering (blue sky, red sunsets), Mie scattering (haze, glow around the
// sun) and ozone absorption (deep blue twilight), with densities given as a
// function of height only. Precomputes, on the CPU:
//   - transmittance from any height and direction to space (256 x 64), and
//   - multiple scattering, isotropic, per height and sun angle (32 x 32),
// which the atmosphere shader looks up while marching each view ray. Also
// answers on the CPU what the sunlight and the sky's ambient light are at a
// point (for the scene's lights).
//
// Units: meters; light per unit of sunlight (multiply by the sun's color and
// intensity). Heights are given as fractions of the atmosphere's thickness:
// game planets are small, and the same profile then works at any size.
struct AtmosphereParams
{
    double bottom = 1000.0;                       // planet (sea level) radius
    double top = 1250.0;                          // outer radius
    glm::dvec3 rayleighDepth{0.02, 0.045, 0.11};  // vertical optical depth
    double rayleighHeight = 0.4;                  // scale height / thickness
    double mieDepth = 0.006;
    double mieHeight = 0.15;
    double mieAnisotropy = 0.7;
    double mieAbsorption = 0.11;                  // absorbed per scattered
    glm::dvec3 ozoneDepth{0.0042, 0.0117, 0.0005}; // vertical optical depth (absorption)
    double ozoneCenter = 0.35;                    // height of the ozone layer's peak / thickness
    double ozoneWidth = 0.3;                      // its full width / thickness
    glm::dvec3 groundAlbedo{0.3};
    // Air counted this many times over on sunlight's way in (not on the way to
    // the camera): a game planet is so small that sunlight crosses little air
    // even at the horizon, so its sunsets would be pale; this reddens them
    // (and the sky and terrain they light) without hazing nearby terrain.
    double sunsetStrength = 5.0;

    // The radii within a millionth: a turning planet's radius (from its
    // transform's scale) wobbles in the last bits, which must not rebuild.
    bool operator==(const AtmosphereParams &o) const;
};

class AtmosphereModel
{
  public:
    static constexpr int TransmittanceWidth = 256;
    static constexpr int TransmittanceHeight = 64;
    static constexpr int MultiScatteringSize = 32;

    void build(const AtmosphereParams &params);
    const AtmosphereParams &params() const { return _p; }
    bool is_built() const { return !_transmittance.empty(); }

    // Transmittance from radius r along a direction with zenith cosine mu to
    // space (the ground not counted).
    glm::dvec3 transmittance(double r, double mu) const;
    // Sunlight left at radius r for a sun at zenith cosine muSun: zero below
    // the planet's horizon.
    glm::dvec3 sunlight(double r, double muSun) const;
    // Multiple scattering (per unit of sunlight and of scattering coefficient).
    glm::dvec3 multiple_scattering(double r, double muSun) const;
    // The sky's average radiance over the upper hemisphere (cosine weighted)
    // at radius r: what an ambient light should be, per unit of sunlight.
    glm::dvec3 sky_ambient(double r, double muSun) const;

    Ref<Image> transmittance_image() const;
    Ref<Image> multiple_scattering_image() const;

    // Medium at height h above sea level (per meter).
    struct Medium
    {
        glm::dvec3 rayleighScattering, mieScattering, extinction;
    };
    Medium medium(double h) const;

  private:
    AtmosphereParams _p;
    glm::dvec3 _rayleigh{0.0}; // scattering per meter at sea level
    double _mie = 0.0;
    glm::dvec3 _ozone{0.0};    // absorption per meter at the peak
    std::vector<glm::dvec3> _transmittance;
    std::vector<glm::dvec3> _multiScattering;

    glm::dvec3 lookup(const std::vector<glm::dvec3> &table, int width, int height, double u, double v) const;
    void transmittance_uv(double r, double mu, double &u, double &v) const;
    // Radiance along a ray (single scattering of the sun plus multiple
    // scattering), per unit of sunlight, from radius r towards zenith cosine
    // mu, with the sun at zenith cosine muSun and nu between ray and sun.
    glm::dvec3 ray_radiance(double r, double mu, double muSun, double nu, int steps) const;
};

#endif // JAR_ATMOSPHERE_MODEL_H
