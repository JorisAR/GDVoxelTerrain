shader_type spatial;
render_mode world_vertex_coords;

//#include "res://Addons/CodeJar/Shaders/Functions/triplanarSampling.gdshaderinc"
#include "res://addons/jar_voxel_terrain/src/shaders/color_transformation.gdshaderinc"


uniform vec4 sand_color : source_color;
uniform vec4 rock_color : source_color;
uniform vec4 grass_color : source_color;
uniform vec4 dirt_color : source_color;
uniform vec3 world_center = vec3(0,-4196,0);
uniform float world_radius = 4096;

// Painted materials: vertex COLOR carries 4 blend weights (one per material),
// written by terrain edits (see JarVoxelTerrain.sphere_edit's material index).
// All-zero weights = untouched terrain -> procedural coloring below.
uniform vec4 material_1_color : source_color = vec4(0.45, 0.30, 0.18, 1.0); // dirt
uniform vec4 material_2_color : source_color = vec4(0.42, 0.44, 0.48, 1.0); // stone
uniform vec4 material_3_color : source_color = vec4(0.85, 0.65, 0.15, 1.0); // ore
uniform vec4 material_4_color : source_color = vec4(0.55, 0.25, 0.85, 1.0); // crystal
uniform float material_1_roughness : hint_range(0.0, 1.0) = 0.9;
uniform float material_2_roughness : hint_range(0.0, 1.0) = 0.8;
uniform float material_3_roughness : hint_range(0.0, 1.0) = 0.3;
uniform float material_4_roughness : hint_range(0.0, 1.0) = 0.15;

//varying vec3 global_normal;
varying vec3 global_position;


void vertex() {
	global_position = VERTEX;
}

void fragment() {
	vec3 global_normal = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec3 WORLD_UP = normalize(global_position - world_center);
	float WORLD_HEIGHT  = length(global_position - world_center) - world_radius;

	SPECULAR = 0.1;
	ROUGHNESS = 0.8;

	vec3 color = rock_color.rgb;
	if (dot(WORLD_UP, global_normal)  > 0.85f) {
		color = grass_color.rgb;
	}
	if (WORLD_HEIGHT < -20.0) {
		color = sand_color.rgb;
	}

	// Painted material blend. COLOR holds up to 4 material weights that were
	// interpolated across edges/LODs by the mesher, so material boundaries
	// transition smoothly instead of hard-switching.
	float painted = COLOR.r + COLOR.g + COLOR.b + COLOR.a;
	if (painted > 0.001) {
		vec4 w = COLOR / painted;
		vec3 mat_color = w.r * material_1_color.rgb + w.g * material_2_color.rgb
		               + w.b * material_3_color.rgb + w.a * material_4_color.rgb;
		float mat_rough = w.r * material_1_roughness + w.g * material_2_roughness
		                + w.b * material_3_roughness + w.a * material_4_roughness;
		float blend = clamp(painted, 0.0, 1.0);
		color = mix(color, mat_color, blend);
		ROUGHNESS = mix(0.8, mat_rough, blend);
	}

	NORMAL = normalize((VIEW_MATRIX * vec4(global_normal, 0.0)).xyz);
	ALBEDO = color;
}