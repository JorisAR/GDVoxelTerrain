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
	// Edited color
	if(COLOR.r > 0.5) {
		color = dirt_color.rgb;
		SPECULAR = 0.1;
		ROUGHNESS = 0.8;
	}

	NORMAL = normalize((VIEW_MATRIX * vec4(global_normal, 0.0)).xyz);
	ALBEDO = color;
}