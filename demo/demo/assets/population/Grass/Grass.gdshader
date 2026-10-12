shader_type spatial;

render_mode cull_disabled, world_vertex_coords;

#include "res://addons/jar_voxel_terrain/shaders/functions/WindDisplacement.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/triplanarSampling.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/ColorTransformations.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/jar_population.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/Dither.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/GrassDisplacement.gdshaderinc"

uniform sampler2D noise;
uniform sampler2D albedo_texture : source_color, hint_default_white;
uniform float scale = 1.0;
uniform float normalScale = 1.0;

varying vec3 global_position;
varying vec3 grass_displacement;

void vertex() {
	grass_displacement = 1.0 - sampleGrassDisplacement(VERTEX).rgb;
	float height_multiplier = grass_displacement.r * 0.75 + 0.25;
	VERTEX += get_wind_displacement(VERTEX, 0.3) * COLOR.r * COLOR.r * COLOR.r * height_multiplier;

	vec4 local = inverse(MODEL_MATRIX) * vec4(VERTEX, 1.0);
	local.y *= height_multiplier;
	VERTEX = (MODEL_MATRIX * local).xyz;
	global_position = VERTEX;
	JAR_POPULATION_VERTEX
}

void fragment() {
	//if(dither(SCREEN_UV * VIEWPORT_SIZE,
		//smoothstep(64, 80, distance(global_position, CAMERA_POSITION_WORLD))
		//))
		//discard;

	NORMAL = FRONT_FACING ? NORMAL : -NORMAL;

	//vec3 sampled_noise = sampleFractalYPlanar(noise, global_position, scale, CAMERA_POSITION_WORLD, vec3(0.0)).rgb;
	vec3 sampled_noise = sample_y_planar(noise, global_position, scale).rgb;
	// The terrain's color under the clump (from the volume).
	vec3 grass_color = jar_ground_color(vec3(0.12, 0.25, 0.05));

	vec3 main_color = mix(vec3(0.015,0.015,0.015), grass_color, grass_displacement.g);

	vec3 col = mix(grass_color, main_color.rgb, min(1.0, COLOR.r * 1.5));
	ALBEDO = noiseColorTransformer(col, sampled_noise) * texture(albedo_texture, UV).rgb;
	SPECULAR = 0.1;
	ROUGHNESS = 0.8;
}
