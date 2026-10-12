shader_type spatial;

render_mode world_vertex_coords;

#include "res://addons/jar_voxel_terrain/shaders/jar_population.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/ColorTransformations.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/triplanarSampling.gdshaderinc"

uniform float specular : hint_range(0, 1) = 0.5f;
uniform float roughness : hint_range(0, 1) = 0.5f;
uniform float metalic : hint_range(0, 1) = 0.0f;
uniform vec4 albedo : source_color = vec4(1.0);

uniform sampler2D noise;
uniform float noiseScale = 0.25;
uniform float normalScale = 0.15;

varying vec3 global_position;

void vertex() {
	global_position = VERTEX;
	JAR_POPULATION_VERTEX
}

void fragment() {
	vec3 global_normal = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);

	SPECULAR = specular;
	ROUGHNESS = roughness;
	METALLIC = metalic;
	vec3 sampled_noise = sampleTriplanar(noise, global_position, global_normal, noiseScale).rgb;

	global_normal = noiseNormalTransformer(global_normal, sampled_noise, normalScale);
	ALBEDO = noiseColorTransformer(albedo.rgb, sampled_noise);
	NORMAL = normalize((VIEW_MATRIX * vec4(global_normal, 0.0)).xyz);
}
