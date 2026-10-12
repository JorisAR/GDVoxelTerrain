shader_type spatial;

render_mode cull_disabled, world_vertex_coords;

// Vertex colors: r = leaves (1) or trunk (0), g = height up the tree (for
// wind and for blending the trunk's base into the ground).

//Set to false to disable dynamic features, good for imposter rendering
#define IsDynamic true

#include "res://addons/jar_voxel_terrain/shaders/jar_population.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/WindDisplacement.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/ColorTransformations.gdshaderinc"
#include "res://addons/jar_voxel_terrain/shaders/functions/Dither.gdshaderinc"

varying float hash;
varying float id;

uniform vec4 leaves_color1 : source_color;
uniform vec4 leaves_color2 : source_color;
uniform vec4 leaves_highlight : source_color;
uniform vec4 trunk_color : source_color;
uniform float leaves_highlight_strength : hint_range(0.0, 1.0, 0.1);
uniform float black_spots_strength : hint_range(0.0, 1.0, 0.1);
uniform float ground_factor : hint_range(1.0, 50.0, 0.1) = 20.0;

uniform sampler2D noise;
uniform sampler2D leaves_mask : repeat_disable;

uniform float wind_affect = 0.5;
uniform float dissolve : hint_range(0.0, 1.0, 0.01);

float cheapHash(vec3 pos) {
	return sin(dot(pos, vec3(34,54,27)) + TIME * 1.0f);
}


void vertex() {
#if  IsDynamic
	hash = cheapHash(VERTEX);
	VERTEX += get_wind_displacement(VERTEX, wind_affect + 5.0 * dissolve) * COLOR.g * COLOR.g;
	// Sink while dissolving, along the tree's own up.
	VERTEX -= normalize(MODEL_MATRIX[1].xyz) * (2.5 * dissolve) * (0.9 + 0.025 * dot(VERTEX - MODEL_MATRIX[3].xyz, normalize(MODEL_MATRIX[1].xyz)));
	id = sin(1.1123 * dot(MODEL_MATRIX[3].xyz, vec3(43,54,12))); // the instance (NODE_POSITION_WORLD is the multimesh)
#endif
	JAR_POPULATION_VERTEX
}

void fragment() {
	if(COLOR.r < 0.5 && (!FRONT_FACING || dissolve > 0.0f)) discard;

	NORMAL = FRONT_FACING ? NORMAL : -NORMAL;
	vec3 mask_color = COLOR.r > 0.5 ? texture(leaves_mask, UV + vec2(hash) * 0.015f).rgb : vec3(1.0,1.0,1.0);
	if(mask_color.r < 0.5 || dither(SCREEN_UV * VIEWPORT_SIZE, dissolve)) discard;
	vec3 noise_sample = texture(noise, UV).rgb;
	// The terrain's color where the tree stands (from the volume).
	vec3 ground_color = jar_ground_color(trunk_color.rgb);

	vec3 color = (COLOR.r > 0.5)
		?	mix(mix(leaves_color1.rgb, leaves_color2.rgb, id), leaves_highlight.rgb,
			min(mask_color.g, leaves_highlight_strength))
		: trunk_color.rgb;

	float blackSpots = 1.0 - black_spots_strength * (1.0 - COLOR.r) * (1.0 - smoothstep(0.1, 0.2, noise_sample.r));
	vec3 transformed_color = noiseColorTransformer(color, noise_sample) * blackSpots;

	ALBEDO = mix(transformed_color, ground_color, max(0.0, 1.0-ground_factor*COLOR.g));
}
