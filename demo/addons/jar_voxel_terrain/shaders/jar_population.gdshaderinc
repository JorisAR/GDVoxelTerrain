// Shader support for JarTerrainFeature and JarTerrainDetail materials.
//
//   #include "res://addons/jar_voxel_terrain/shaders/jar_population.gdshaderinc"
//   void vertex() {
//       ...
//       JAR_POPULATION_VERTEX   // last: hides instances outside their LOD range
//   }
//   void fragment() {
//       JAR_POPULATION_FRAGMENT // first: fades levels into each other
//       vec3 ground = jar_ground_color(vec3(0.3, 0.4, 0.2)); // the terrain's color under the instance
//   }
//
// Levels of detail (a scene's MeshInstance3D visibility ranges, a detail's
// far mesh) are drawn as one multimesh per cell or chunk. With this include,
// each instance shows between its own distances, measured from the main
// camera (shadow passes agree), fading in and out by dithering; without it
// whole cells switch at once.
//
// The volume writes each instance's custom data (INSTANCE_CUSTOM):
//   x: ground color, sqrt-encoded 8 bits per channel packed into an integer
//   y, z: LOD range (meters from the main camera)
//   w: 1 + the instance's random number in [0, 1) (0 when not drawn by a
//      volume: then nothing is hidden)

varying vec4 jar_instance_data;

// How wide levels fade into each other, as a part of the distance they switch at.
const float JAR_FADE_BAND = 0.1;

varying vec2 jar_fade; // how far in (1: all) and out (0: none) of its fade this instance is

// Collapses the vertex (no triangles drawn) when the instance is outside its
// LOD range; near the ends of it, sets how far it has faded. `model` is
// MODEL_MATRIX (the instance's transform).
vec2 jar_population_lod(mat4 model, mat4 main_camera, vec4 custom, inout vec3 vertex) {
	vec2 fade = vec2(1.0, 0.0);
	if (custom.w < 0.5)
		return fade;
	float distance_to_camera = distance(model[3].xyz, main_camera[3].xyz);
	if (custom.y > 0.0) {
		float band = custom.y * JAR_FADE_BAND;
		fade.x = clamp((distance_to_camera - custom.y + band * 0.5) / band, 0.0, 1.0);
	}
	if (custom.z < 1e19) {
		float band = custom.z * JAR_FADE_BAND;
		fade.y = clamp((distance_to_camera - custom.z + band * 0.5) / band, 0.0, 1.0);
	}
	if (fade.x <= 0.0 || fade.y >= 1.0) {
		vertex = vec3(0.0);
	}
	return fade;
}

// Screen-door noise: interleaved gradient noise of the pixel, in [0, 1).
float jar_dither(vec2 pixel) {
	return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Call at the end of vertex() (after anything else that moves VERTEX).
#define JAR_POPULATION_VERTEX jar_instance_data = INSTANCE_CUSTOM; jar_fade = jar_population_lod(MODEL_MATRIX, MAIN_CAM_INV_VIEW_MATRIX, INSTANCE_CUSTOM, VERTEX);

// Call first in fragment(): fades by leaving out pixels, never by blending. A
// level fading out and the next fading in use opposite pixels of the same
// noise, so together they cover everything, with no holes.
#define JAR_POPULATION_FRAGMENT if (jar_fade.x < 1.0 || jar_fade.y > 0.0) { float jar_noise = jar_dither(FRAGCOORD.xy); if (jar_noise >= jar_fade.x || jar_noise < jar_fade.y) { discard; } }

// The instance's random number in [0, 1): fixed to where it stands in its
// volume, the same for its mesh and its impostor. Use it for per-instance
// variation instead of hashing world positions (floating origin shifts and
// turning planets change those). `fallback` when not drawn by a volume.
float jar_instance_random(vec4 custom, float fallback) {
	return custom.w >= 0.5 ? fract(custom.w) : fallback;
}

// The ground color under this instance (linear), or `fallback` where the
// volume did not provide one (e.g. the mesh is not drawn by a volume).
vec3 jar_ground_color(vec3 fallback) {
	if (jar_instance_data.w < 0.5)
		return fallback;
	uint packed = uint(jar_instance_data.x + 0.5);
	vec3 encoded = vec3(float((packed >> 16u) & 255u), float((packed >> 8u) & 255u), float(packed & 255u)) / 255.0;
	return encoded * encoded;
}
