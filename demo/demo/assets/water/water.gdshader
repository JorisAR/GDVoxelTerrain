shader_type spatial;

#include "res://addons/jar_voxel_terrain/src/shaders/color_transformation.gdshaderinc"

uniform sampler2D cellNoise;
uniform sampler2D noise;
uniform float scale = 1.0;
uniform float normalScale = 1.0;
uniform vec4 shallow_water_color : source_color;
uniform vec4 deep_water_color : source_color;

uniform sampler2D DEPTH_TEXTURE : hint_depth_texture;
uniform sampler2D SCREEN_TEXTURE : hint_screen_texture, filter_nearest, repeat_disable;

void vertex() {
	// Called for every vertex the material is visible on.
}

float SchlickFresnel(float u) {
	float m = 1.0 - u;
	float m2 = m * m;
	return m2 * m2 * m;
}

float depth_to_foam(float depth, vec3 _noise) {
	return sin(-0.75f * TIME + depth * 5.0 + _noise.r * 10.0) + 1.0f * (0.8f - depth) + 2.0f * (_noise.g - 0.5f) > 0.0f ? 1.0f : 0.0f;
}

void fragment() {
	vec3 global_normal = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
    vec3 global_pos = (INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz + NODE_POSITION_WORLD;

	//Foam
	float depth = texture(DEPTH_TEXTURE, SCREEN_UV).r;
	depth = PROJECTION_MATRIX[3][2] / (depth + PROJECTION_MATRIX[2][2]);
	depth = VERTEX.z + depth;
	float foam = depth_to_foam(depth, vec3(0.0f));

	//Water color
	float VdotN = dot(VIEW, NORMAL);
	float fresnel = clamp(SchlickFresnel(VdotN), 0.0, 1.0);
	//float depthFactor = clamp(fresnel * depth * 0.1f, 0.0f, 1.0f);
	float depthFactor = pow(clamp(1.0f - fresnel - depth * 0.05, 0.0f, 1.0f), 4.0);
	vec3 color = depthFactor * shallow_water_color.rgb + (1.0f - depthFactor) * deep_water_color.rgb;
	color = noiseColorTransformer(color, vec3(0.0f));

	float alpha = (1.0f - foam) * (depthFactor * 0.1f + (1.0f - depthFactor) * 0.98f) + foam * 1.0f;
	color = (1.0f - foam) * color + foam * vec3(1.0f);

	NORMAL = normalize((VIEW_MATRIX * vec4(global_normal, 0.0)).xyz);

	vec3 refraction = texture(SCREEN_TEXTURE, SCREEN_UV + (NORMAL.xz * 0.1)).rgb;
	ALBEDO = refraction * (1.0 - alpha) + color * alpha;

	SPECULAR = 0.6f;
	ROUGHNESS = 0.5f;
}
