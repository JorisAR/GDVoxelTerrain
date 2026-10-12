shader_type spatial;

render_mode cull_disabled, world_vertex_coords;

//Set to false to disable dynamic features, good for imposter rendering
#define IsDynamic true

#include "res://addons/jar_voxel_terrain/shaders/jar_population.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/WindDisplacement.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/ColorTransformations.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/Dither.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/GrassDisplacement.gdshaderinc"

varying vec3 global_position;
varying vec3 global_normal;
varying vec3 grass_displacement;
varying float hash;
varying float id;

uniform bool flip_normal;
uniform vec4 leaves_color1 : source_color;
uniform vec4 leaves_color2 : source_color;
uniform vec4 leaves_highlight : source_color;
uniform vec4 trunk_color : source_color;
uniform float leaves_highlight_strength : hint_range(0.0, 1.0, 0.1);
uniform float ground_factor : hint_range(1.0, 50.0, 0.1) = 20.0;

uniform sampler2D noise;
uniform sampler2D leaves_mask : repeat_disable;

uniform float wind_affect = 0.5;

float cheapHash(vec3 pos) {
	return sin(dot(pos, vec3(34,54,27)) + TIME * 1.0f);
}

void vertex() {
	grass_displacement = 1.0 - sampleGrassDisplacement(VERTEX).rgb;
	//grass_displacement = vec3(1.0);
	float height_multiplier = grass_displacement.r * 0.75 + 0.25;
	VERTEX += get_wind_displacement(VERTEX, wind_affect) * COLOR.g * COLOR.g * height_multiplier;

	vec4 local = inverse(MODEL_MATRIX) * vec4(VERTEX, 1.0);
	local.y = mix(0.05 - local.y * height_multiplier, local.y, grass_displacement.r);
	VERTEX = (MODEL_MATRIX * local).xyz;
	global_position = VERTEX;

	hash = cheapHash(VERTEX);



	id = sin(1.1123 * dot(MODEL_MATRIX[3].xyz, vec3(43,54,12))); // the instance (NODE_POSITION_WORLD is the multimesh)

	global_position = VERTEX;
	global_normal = VERTEX;
	JAR_POPULATION_VERTEX
}

void fragment() {
	NORMAL = FRONT_FACING || !flip_normal ? NORMAL : -NORMAL;
	vec3 mask_color = vec3(1.0,1.0,1.0);
	vec3 noise_sample = vec3(1.0,1.0,1.0);
	vec3 ground_color = trunk_color.rgb;

	mask_color = COLOR.r > 0.5 ? texture(leaves_mask, UV + vec2(hash) * 0.015f).rgb : vec3(1.0,1.0,1.0);
	if(mask_color.r < 0.5) discard;
	noise_sample = texture(noise, UV).rgb;
	ground_color = jar_ground_color(trunk_color.rgb);

	vec3 color = (COLOR.r > 0.5)
		?	mix(mix(leaves_color1.rgb, leaves_color2.rgb, id), leaves_highlight.rgb,
			min(mask_color.g, leaves_highlight_strength))
		: trunk_color.rgb;

	color = mix(noiseColorTransformer(color, noise_sample), ground_color, max(0.0, 1.0-ground_factor*COLOR.g));
	ALBEDO = mix(vec3(0.015,0.015,0.015), color, grass_displacement.g);
}