shader_type spatial;
render_mode world_vertex_coords;

// Terrain has no UVs; all textures are sampled triplanar in world space.
// Procedural layers (grass / cliff / sand / snow / transition) dress untouched
// terrain by slope + height. Painted materials (see JarVoxelTerrain.sphere_edit
// material argument) arrive as 4 blend weights in vertex COLOR:
//   r = dirt, g = stone, b = ore (tinted stone), a = crystal (tinted snow).

uniform vec3 world_center = vec3(0, -4196, 0);
uniform float world_radius = 4096;
uniform float texture_scale = 0.12;

// Procedural layer textures.
uniform sampler2D grass_tex : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D cliff_tex : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D sand_tex : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D snow_tex : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D clover_tex : source_color, filter_linear_mipmap, repeat_enable;

// Painted material textures + tints (tint white = texture as-is).
uniform sampler2D dirt_tex : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D stone_tex : source_color, filter_linear_mipmap, repeat_enable;
uniform vec4 ore_tint : source_color = vec4(1.9, 1.35, 0.35, 1.0);
uniform vec4 crystal_tint : source_color = vec4(0.75, 0.35, 1.3, 1.0);
uniform float material_1_roughness : hint_range(0.0, 1.0) = 0.95; // dirt
uniform float material_2_roughness : hint_range(0.0, 1.0) = 0.85; // stone
uniform float material_3_roughness : hint_range(0.0, 1.0) = 0.4;  // ore
uniform float material_4_roughness : hint_range(0.0, 1.0) = 0.15; // crystal

// Procedural thresholds (world height is distance from world_center - radius).
uniform float snow_height = 112.0;
uniform float sand_height = -20.0;
uniform float blend_sharpness = 4.0;

varying vec3 global_position;

void vertex() {
	global_position = VERTEX;
}

vec3 triplanar(sampler2D tex, vec3 pos, vec3 blend_w) {
	vec3 x = texture(tex, pos.yz).rgb;
	vec3 y = texture(tex, pos.xz).rgb;
	vec3 z = texture(tex, pos.xy).rgb;
	return x * blend_w.x + y * blend_w.y + z * blend_w.z;
}

void fragment() {
	vec3 global_normal = normalize((INV_VIEW_MATRIX * vec4(NORMAL, 0.0)).xyz);
	vec3 WORLD_UP = normalize(global_position - world_center);
	float WORLD_HEIGHT = length(global_position - world_center) - world_radius;

	vec3 tp = global_position * texture_scale;
	vec3 blend_w = pow(abs(global_normal), vec3(blend_sharpness));
	blend_w /= (blend_w.x + blend_w.y + blend_w.z);

	SPECULAR = 0.1;
	float rough = 0.9;

	// --- Procedural layers (slope + height driven) ---
	float up = dot(WORLD_UP, global_normal);
	float slope_t = smoothstep(0.62, 0.9, up);          // 0 = cliff, 1 = flat
	float snow_t = smoothstep(snow_height - 3.0, snow_height + 3.0, WORLD_HEIGHT);
	float sand_t = 1.0 - smoothstep(sand_height - 2.0, sand_height + 2.0, WORLD_HEIGHT);

	vec3 flat_col = triplanar(grass_tex, tp, blend_w);
	// Clover-dirt eases the grass->cliff transition band.
	float band = smoothstep(0.55, 0.75, up) * (1.0 - smoothstep(0.75, 0.92, up));
	flat_col = mix(flat_col, triplanar(clover_tex, tp, blend_w), band * 0.6);

	vec3 color = mix(triplanar(cliff_tex, tp, blend_w), flat_col, slope_t);
	color = mix(color, triplanar(sand_tex, tp, blend_w), sand_t);
	// Snow settles on walkable slopes; cliffs shed it.
	color = mix(color, triplanar(snow_tex, tp, blend_w), snow_t * smoothstep(0.35, 0.6, up));
	rough = mix(0.9, 0.6, snow_t);

	// --- Painted materials (vertex COLOR weights from terrain edits) ---
	float painted = COLOR.r + COLOR.g + COLOR.b + COLOR.a;
	if (painted > 0.001) {
		vec4 w = COLOR / painted;
		vec3 stone_sample = triplanar(stone_tex, tp, blend_w);
		vec3 snow_sample = triplanar(snow_tex, tp * 1.6, blend_w);
		vec3 mat_color = w.r * triplanar(dirt_tex, tp, blend_w)
		               + w.g * stone_sample
		               + w.b * stone_sample * ore_tint.rgb
		               + w.a * snow_sample * crystal_tint.rgb;
		float mat_rough = w.r * material_1_roughness + w.g * material_2_roughness
		                + w.b * material_3_roughness + w.a * material_4_roughness;
		float blend = clamp(painted, 0.0, 1.0);
		color = mix(color, mat_color, blend);
		rough = mix(rough, mat_rough, blend);
	}

	NORMAL = normalize((VIEW_MATRIX * vec4(global_normal, 0.0)).xyz);
	ALBEDO = color;
	ROUGHNESS = rough;
}
