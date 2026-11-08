#ifndef JAR_CLOUD_NOISE_H
#define JAR_CLOUD_NOISE_H

#include <godot_cpp/classes/image_texture3d.hpp>

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
} // namespace CloudNoise

#endif // JAR_CLOUD_NOISE_H
