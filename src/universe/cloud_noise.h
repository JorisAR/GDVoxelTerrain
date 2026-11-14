#ifndef JAR_CLOUD_NOISE_H
#define JAR_CLOUD_NOISE_H

#include <godot_cpp/classes/cubemap.hpp>
#include <godot_cpp/classes/image_texture3d.hpp>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

using namespace godot;

// The 3D noise volumetric clouds are made of (JarBodyClouds), tileable in all
// three directions so it repeats seamlessly:
// - r: Perlin-Worley (billowy, connected blobs: the clouds' large shapes),
// - g, b, a: Worley noise at 2, 4 and 8 times that frequency (puffs that
//   erode the edges into cauliflower detail).
// Made once per run on all cores, in a few tens of milliseconds, with mipmaps.
namespace CloudNoise
{
constexpr int Size = 64;
Ref<ImageTexture3D> texture();

// A planet's weather as a cube map (per direction from its center, in the
// body's own space; `pole`: its spin axis there), made on all cores in a
// fraction of a second:
// - r: coverage, by climate zone (noisy borders): clusters of storms along
//   the equator, mostly clear subtropics with patches of cellular low cloud,
//   cyclones (spiral, with fronts drawn out east to west) in the storm
//   belts, stratus over the poles;
// - g: how high the tops reach (tall equatorial towers, low decks);
// - b: how sheet-like they are (0 heaped cumulus, 1 smooth stratus).
// `size`: texels per face edge.
Ref<Cubemap> weather(int seed, const Vector3 &pole, int size = 512);
// The same in two parts: the texels (any thread; it uses all cores), then
// the texture (main thread).
using WeatherFaces = std::vector<std::vector<uint8_t>>;
WeatherFaces weather_faces(int seed, const Vector3 &pole, int size = 512);
Ref<Cubemap> weather_texture(const WeatherFaces &faces, int size = 512);

// A gas giant's cloud bands as a cube map (same layout, any thread; made
// into a texture with weather_texture): r how dark (belts vs zones), g how
// much of the accent color, b white ovals, a dark festoons. A randomized band
// layout with uneven widths, its edges warped by sheared turbulence and
// curled by eddies, filaments inside the bands, chains of white ovals.
WeatherFaces band_faces(int seed, const Vector3 &pole, float band_count, float turbulence, float storms,
                        int size = 512);

// Faces made on a detached thread. The owner may be freed before they are
// done (the result is then dropped): nothing ever waits for the thread (a
// std::future from std::async blocked the main thread in its destructor).
class FacesJob
{
    struct State
    {
        std::atomic<bool> done{false};
        WeatherFaces faces;
    };
    std::shared_ptr<State> _state;

  public:
    template <typename Make> void start(Make make)
    {
        auto state = std::make_shared<State>();
        _state = state;
        std::thread([state, make] {
            state->faces = make();
            state->done = true;
        }).detach();
    }
    bool running() const { return _state != nullptr; }
    bool ready() const { return _state && _state->done.load(); }
    WeatherFaces take()
    {
        WeatherFaces faces = std::move(_state->faces);
        _state.reset();
        return faces;
    }
};
} // namespace CloudNoise

#endif // JAR_CLOUD_NOISE_H
