shader_type spatial;

render_mode cull_disabled, world_vertex_coords;

#include "res://addons/jar_voxel_terrain/shaders/functions/WindDisplacement.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/ColorTransformations.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/jar_population.gdshaderinc"

varying vec3 global_position;
varying float hash;
varying float id;

uniform vec4 leaves_color1 : source_color;
uniform vec4 leaves_color2 : source_color;
uniform vec4 trunk_color : source_color;

uniform sampler2D noise;
uniform sampler2D leaves_mask : repeat_disable;
uniform sampler2D stem_mask;

uniform float wind_affect = 0.5;


float cheapHash(vec3 pos) {
	return sin(dot(pos, vec3(34,54,27)) + TIME * 1.0f);
}

void vertex() {
	hash = cheapHash(VERTEX);
	VERTEX += get_wind_displacement(VERTEX, wind_affect) * pow(COLOR.g, 2.0);
	global_position = VERTEX;
	id = sin(1.1123 * dot(MODEL_MATRIX[3].xyz, vec3(43,54,12))); // the instance (NODE_POSITION_WORLD is the multimesh)
	JAR_POPULATION_VERTEX
}

void fragment() {
	vec3 col = COLOR.r > 0.5 ? texture(leaves_mask, UV).rgb : (
		COLOR.b > 0.5 ? texture(stem_mask, UV * 3.0).rgb : vec3(1.0));
	if(col.r < 0.5) discard;

	vec3 color = (COLOR.r > 0.5) ?
		mix(leaves_color1.rgb, leaves_color2.rgb, id) :
		trunk_color.rgb;

	vec3 ground_col = jar_ground_color(trunk_color.rgb);

	ALBEDO = mix(mix(color, noiseColorTransformer(color, texture(noise, UV).rgb), 0.25), ground_col, (1.0- 2.0 *COLOR.g) * ((1.0-COLOR.r) * (1.0-COLOR.b)));
}