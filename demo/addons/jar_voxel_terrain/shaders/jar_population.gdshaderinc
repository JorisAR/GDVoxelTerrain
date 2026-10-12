// Shader support for JarTerrainFeature and JarTerrainDetail materials.
//
//   #include "res://addons/jar_voxel_terrain/shaders/jar_population.gdshaderinc"
//   void vertex() {
//       ...
//       JAR_POPULATION_VERTEX   // last: hides instances outside their LOD range
//   }
//   void fragment() {
//       vec3 ground = jar_ground_color(vec3(0.3, 0.4, 0.2)); // the terrain's color under the instance
//   }
//
// Levels of detail (a scene's MeshInstance3D visibility ranges, a detail's
// far mesh) are drawn as one multimesh per cell or chunk. With this include,
// each instance shows exactly between its own distances, measured from the
// main camera (shadow passes agree); without it whole cells switch at once.
//
// The volume writes each instance's custom data (INSTANCE_CUSTOM):
//   x: ground color, sqrt-encoded 8 bits per channel packed into an integer
//   y, z: LOD range (meters from the main camera)
//   w: 1 (0 when not drawn by a volume: then nothing is hidden)

varying vec4 jar_instance_data;

// Collapses the vertex (no triangles drawn) when the instance is outside its
// LOD range. `model` is MODEL_MATRIX (the instance's transform).
void jar_population_lod(mat4 model, mat4 main_camera, vec4 custom, inout vec3 vertex) {
	float distance_to_camera = distance(model[3].xyz, main_camera[3].xyz);
	if (custom.w > 0.5 && (distance_to_camera < custom.y || distance_to_camera >= custom.z)) {
		vertex = vec3(0.0);
	}
}

// Call at the end of vertex() (after anything else that moves VERTEX).
#define JAR_POPULATION_VERTEX jar_instance_data = INSTANCE_CUSTOM; jar_population_lod(MODEL_MATRIX, MAIN_CAM_INV_VIEW_MATRIX, INSTANCE_CUSTOM, VERTEX);

// The ground color under this instance (linear), or `fallback` where the
// volume did not provide one (e.g. the mesh is not drawn by a volume).
vec3 jar_ground_color(vec3 fallback) {
	if (jar_instance_data.w < 0.5)
		return fallback;
	uint packed = uint(jar_instance_data.x + 0.5);
	vec3 encoded = vec3(float((packed >> 16u) & 255u), float((packed >> 8u) & 255u), float(packed & 255u)) / 255.0;
	return encoded * encoded;
}
