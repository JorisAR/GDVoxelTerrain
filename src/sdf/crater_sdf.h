#ifndef CRATER_SDF_H
#define CRATER_SDF_H

#include "modifier_sdfs.h"

// Impact craters on top of another SDF (usually a JarTerrainSdf): bowls with
// raised rims, in several sizes (many small, few large). Craters are measured
// along the child's surface (its natural up field: a planet's center, or +Y),
// and applied as a height change, so they follow any terrain underneath and
// cost no extra samples of the child.
class JarCraterSdf : public JarModifierSdf
{
    GDCLASS(JarCraterSdf, JarModifierSdf);

  private:
    float _size = 40.0f;   // radius of the largest craters, meters
    int _octaves = 3;      // each next octave: half the radius, four times as many
    float _density = 0.35f; // chance of a crater per cell
    float _depth = 0.3f;   // bowl depth as a fraction of the radius
    float _rim = 0.1f;     // rim height as a fraction of the radius
    int _seed = 0;

    float crater_height(const glm::vec3 &pos, const UpField &up) const;

  public:
    float distance(const glm::vec3 &pos) const override;
    Bounds bounds() const override;

    float get_size() const { return _size; }
    void set_size(float v) { set_and_notify(_size, std::max(0.1f, v)); }
    int get_octaves() const { return _octaves; }
    void set_octaves(int v) { set_and_notify(_octaves, std::clamp(v, 1, 8)); }
    float get_density() const { return _density; }
    void set_density(float v) { set_and_notify(_density, std::clamp(v, 0.0f, 1.0f)); }
    float get_depth() const { return _depth; }
    void set_depth(float v) { set_and_notify(_depth, std::clamp(v, 0.0f, 1.0f)); }
    float get_rim() const { return _rim; }
    void set_rim(float v) { set_and_notify(_rim, std::clamp(v, 0.0f, 1.0f)); }
    int get_seed() const { return _seed; }
    void set_seed(int v) { set_and_notify(_seed, v); }

  protected:
    static void _bind_methods();
};

#endif // CRATER_SDF_H
