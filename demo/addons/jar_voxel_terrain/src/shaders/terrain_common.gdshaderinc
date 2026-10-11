// Shared code of the JarVoxelVolume terrain shaders (one per material mode:
// terrain_weights, terrain_split, terrain_indexed). Write your own terrain
// shader by including this file, or by declaring the same inputs:
//
// Set per chunk (instance uniforms):
//   jar_chunk_origin  chunk center in volume space (VERTEX is relative to it)
//   jar_up_center     xyz: up center, w: sea level (height along the axis, or radius)
//   jar_up_axis       xyz: up axis, w: mode (0 axis, 1 radial)
// Set per volume from its JarTerrainMaterials (see TerrainMaterialPalette):
//   jar_palette       256x2: (id, 0) = linear albedo, roughness;
//                            (id, 1) = 1 / texture size, albedo layer, normal layer, variation
//   jar_albedo_textures, jar_normal_textures (+ jar_has_*): optional texture arrays
//
// Everything works in the volume's own space, so textures and rules stay put on
// moving volumes and work the same on flat worlds and planets of any radius.

instance uniform vec3 jar_chunk_origin;
instance uniform vec4 jar_up_center = vec4(0.0);
instance uniform vec4 jar_up_axis = vec4(0.0, 1.0, 0.0, 0.0);

uniform sampler2D jar_palette : filter_nearest;
uniform sampler2DArray jar_albedo_textures : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2DArray jar_normal_textures : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform bool jar_has_albedo_textures = false;
uniform bool jar_has_normal_textures = false;
uniform int jar_material_count = 0;
// Biomes (JarBiomeMap): count, and per-vertex weights of biomes 0-3 (CUSTOM1).
// Biome b's ground rule materials are in the palette at (b, 2) and (b, 3).
uniform int jar_biome_count = 0;

// Unpainted ground is covered by rules. Values are indices into the volume's
// materials; -1 disables a rule.
group_uniforms ground_rules;
uniform int ground_material = 0;
uniform int cliff_material = 1;
uniform int shore_material = 2;
uniform int seabed_material = 3;
uniform int peak_material = 4;
// Ground steeper than this (degrees from up) is cliff.
uniform float cliff_angle : hint_range(0.0, 90.0) = 38.0;
uniform float cliff_blend : hint_range(0.0, 30.0) = 8.0;
// Meters above sea level up to which the shore reaches.
uniform float shore_height = 3.0;
uniform float shore_blend : hint_range(0.0, 50.0) = 2.0;
// Meters below sea level where the seabed starts.
uniform float seabed_depth = 2.0;
uniform float seabed_blend : hint_range(0.0, 50.0) = 2.0;
// Meters above sea level where peaks (snow) start, on ground flatter than cliffs.
uniform float peak_height = 400.0;
uniform float peak_blend : hint_range(0.0, 500.0) = 60.0;
// Noise that breaks up the borders between rule materials.
uniform float rule_noise : hint_range(0.0, 1.0) = 0.5;
uniform float rule_noise_size = 40.0;

group_uniforms look;
uniform float triplanar_sharpness : hint_range(1.0, 16.0) = 4.0;
// Size in meters of the large brightness / hue patches (material variation).
uniform float variation_size = 80.0;
// Size in meters of the fine procedural detail, and how strongly it bumps the normal.
uniform float detail_size = 2.0;
uniform float detail_bump : hint_range(0.0, 2.0) = 0.4;
// Fine detail fades out beyond this distance (it would only shimmer).
uniform float detail_distance = 120.0;
uniform float normal_strength : hint_range(0.0, 2.0) = 1.0;
group_uniforms;

varying vec3 jar_position; // volume space
varying vec3 jar_normal;   // volume space
varying vec4 jar_biomes;   // weights of biomes 0-3

// Use in vertex(): fills jar_position and jar_normal (varyings may only be
// written in the stage functions themselves).
#define JAR_VERTEX jar_position = jar_chunk_origin + VERTEX; jar_normal = NORMAL; jar_biomes = CUSTOM1;

// --- Noise ------------------------------------------------------------------

float jar_hash(vec3 p) {
	p = fract(p * 0.3183099 + 0.1);
	p *= 17.0;
	return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float jar_value_noise(vec3 x) {
	vec3 i = floor(x);
	vec3 f = fract(x);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(jar_hash(i), jar_hash(i + vec3(1, 0, 0)), f.x),
				   mix(jar_hash(i + vec3(0, 1, 0)), jar_hash(i + vec3(1, 1, 0)), f.x), f.y),
			   mix(mix(jar_hash(i + vec3(0, 0, 1)), jar_hash(i + vec3(1, 0, 1)), f.x),
				   mix(jar_hash(i + vec3(0, 1, 1)), jar_hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

float jar_fbm(vec3 x) {
	return 0.6 * jar_value_noise(x) + 0.3 * jar_value_noise(x * 2.03 + 17.1) + 0.1 * jar_value_noise(x * 4.1 + 3.7);
}

// --- Per-pixel context ------------------------------------------------------

struct JarContext {
	vec3 position;  // volume space
	vec3 normal;    // volume space, geometric
	vec3 up;
	float height;   // above sea level
	vec3 triplanar; // projection weights
	float macro;    // 0..1 large patches
	float hue;      // 0..1 large patches
	float detail;   // 0..1 fine detail, faded with distance
	float fade;     // fine detail strength (1 near, 0 far)
	vec4 biomes;    // weights of biomes 0-3
};

// Use in fragment(): JarContext c = jar_context(jar_position, jar_normal, jar_biomes, -VERTEX.z);
JarContext jar_context(vec3 position, vec3 normal, vec4 biomes, float view_distance) {
	JarContext c;
	c.biomes = biomes;
	c.position = position;
	c.normal = normalize(normal);
	bool radial = jar_up_axis.w > 0.5;
	vec3 to_center = position - jar_up_center.xyz;
	c.up = radial ? normalize(to_center) : jar_up_axis.xyz;
	c.height = radial ? length(to_center) - jar_up_center.w : dot(to_center, jar_up_axis.xyz) - jar_up_center.w;
	vec3 w = pow(abs(c.normal), vec3(triplanar_sharpness));
	c.triplanar = w / max(w.x + w.y + w.z, 1e-5);
	c.macro = jar_fbm(position / variation_size);
	c.hue = jar_value_noise(position / (variation_size * 2.3) + 41.0);
	c.fade = 1.0 - smoothstep(detail_distance * 0.5, detail_distance, view_distance);
	c.detail = c.fade > 0.0 ? jar_fbm(position / detail_size) : 0.5;
	return c;
}

// --- Materials --------------------------------------------------------------

struct JarSurface {
	vec3 albedo;
	vec3 normal;
	float roughness;
	float weight;
};

vec4 jar_triplanar(sampler2DArray tex, vec3 p, vec3 w, float layer) {
	return texture(tex, vec3(p.zy, layer)) * w.x + texture(tex, vec3(p.xz, layer)) * w.y
		+ texture(tex, vec3(p.xy, layer)) * w.z;
}

// Whiteout blend of the three projected tangent-space normals.
vec3 jar_triplanar_normal(vec3 p, vec3 n, vec3 w, float layer) {
	vec2 tx = (texture(jar_normal_textures, vec3(p.zy, layer)).xy * 2.0 - 1.0) * normal_strength;
	vec2 ty = (texture(jar_normal_textures, vec3(p.xz, layer)).xy * 2.0 - 1.0) * normal_strength;
	vec2 tz = (texture(jar_normal_textures, vec3(p.xy, layer)).xy * 2.0 - 1.0) * normal_strength;
	vec3 nx = vec3(tx + n.zy, n.x);
	vec3 ny = vec3(ty + n.xz, n.y);
	vec3 nz = vec3(tz + n.xy, n.z);
	return normalize(nx.zyx * w.x + ny.xzy * w.y + nz.xyz * w.z);
}

// Adds material `id` with weight `w` to the surface.
void jar_add_material(inout JarSurface s, JarContext c, int id, float w) {
	if (w < 0.004 || id < 0 || id >= max(jar_material_count, 1))
		return;
	vec4 p0 = texelFetch(jar_palette, ivec2(id, 0), 0);
	vec4 p1 = texelFetch(jar_palette, ivec2(id, 1), 0);
	float variation = jar_material_count > 0 ? p1.a : 0.25;
	vec3 albedo = jar_material_count > 0 ? p0.rgb : vec3(0.3);
	float roughness = jar_material_count > 0 ? p0.a : 0.85;
	// Procedural variation: large brightness and warm/cool patches, fine detail.
	albedo *= 1.0 + variation * (c.macro - 0.5) * 0.9;
	albedo *= mix(vec3(1.0), vec3(1.12, 1.0, 0.82), variation * (c.hue - 0.5));
	albedo *= 1.0 + variation * (c.detail - 0.5) * 0.6 * c.fade;
	vec3 normal = c.normal;
	if (jar_has_albedo_textures && p1.g >= 0.0)
		albedo *= jar_triplanar(jar_albedo_textures, c.position * p1.r, c.triplanar, p1.g).rgb;
	if (jar_has_normal_textures && p1.b >= 0.0)
		normal = jar_triplanar_normal(c.position * p1.r, c.normal, c.triplanar, p1.b);
	s.albedo += albedo * w;
	s.normal += normal * w;
	s.roughness += roughness * w;
	s.weight += w;
}

// Rule material of a biome: its own (palette rows 2-3), or the shader's.
int jar_biome_material(int biome, int rule, int fallback) {
	if (biome >= jar_biome_count || biome > 3)
		return fallback;
	vec4 a = texelFetch(jar_palette, ivec2(biome, 2), 0);
	float id = rule == 0 ? a.x : rule == 1 ? a.y : rule == 2 ? a.z : rule == 3 ? a.w : texelFetch(jar_palette, ivec2(biome, 3), 0).x;
	return id < -0.5 ? fallback : int(round(id));
}

// Covers `amount` of the surface with the ground rules: ground, cliff, shore,
// seabed and peaks by slope and height. With biomes, each biome present here
// uses its own materials for the same rules.
void jar_add_ground(inout JarSurface s, JarContext c, float amount) {
	if (amount < 0.004)
		return;
	float r = (jar_value_noise(c.position / rule_noise_size) - 0.5) * 2.0 * rule_noise;
	float h = c.height + r * max(shore_blend, 1.0) * 2.0;
	float slope_cos = dot(c.normal, c.up) + r * 0.08;
	float cliff_cos = cos(radians(cliff_angle));
	float blend_cos = sin(radians(cliff_angle)) * radians(cliff_blend) * 0.5;
	float cliff = 1.0 - smoothstep(cliff_cos - blend_cos, cliff_cos + blend_cos, slope_cos);
	float seabed = 1.0 - smoothstep(-seabed_depth - seabed_blend, -seabed_depth + seabed_blend, h);
	float shore = 1.0 - smoothstep(shore_height - shore_blend, shore_height + shore_blend, h);
	float peak = smoothstep(peak_height - peak_blend, peak_height + peak_blend, c.height + r * peak_blend);

	int biome_count = jar_biome_count > 0 ? min(jar_biome_count, 4) : 1;
	for (int b = 0; b < biome_count; b++) {
		float weight = jar_biome_count > 0 ? c.biomes[b] : 1.0;
		if (weight < 0.004)
			continue;
		int m_ground = jar_biome_material(b, 0, ground_material);
		int m_cliff = jar_biome_material(b, 1, cliff_material);
		int m_shore = jar_biome_material(b, 2, shore_material);
		int m_seabed = jar_biome_material(b, 3, seabed_material);
		int m_peak = jar_biome_material(b, 4, peak_material);
		// A rule without a material leaves its share to the next rule down.
		float w_seabed = m_seabed >= 0 ? seabed : 0.0;
		float above = 1.0 - w_seabed;
		float w_cliff = m_cliff >= 0 ? above * cliff : 0.0;
		float flat_ground = above - w_cliff;
		float w_shore = m_shore >= 0 ? flat_ground * shore : 0.0;
		float rest = flat_ground - w_shore;
		float w_peak = m_peak >= 0 ? rest * peak : 0.0;
		float w_ground = rest - w_peak;
		float k = amount * weight;
		jar_add_material(s, c, m_seabed, w_seabed * k);
		jar_add_material(s, c, m_cliff, w_cliff * k);
		jar_add_material(s, c, m_shore, w_shore * k);
		jar_add_material(s, c, m_peak, w_peak * k);
		jar_add_material(s, c, m_ground, w_ground * k);
	}
}

// Writes the blended surface. Fine procedural detail bumps the normal (screen
// space derivatives, no extra noise samples).
void jar_finish(JarSurface s, JarContext c, mat4 model, mat4 view, out vec3 albedo, out float roughness,
				out vec3 view_normal) {
	float total = max(s.weight, 1e-4);
	albedo = s.albedo / total;
	roughness = s.roughness / total;
	vec3 n = normalize(s.normal / total);
	if (detail_bump > 0.0 && c.fade > 0.0) {
		// Surface gradient bump mapping (Mikkelsen) with the detail noise as height.
		vec3 dpdx = dFdx(c.position);
		vec3 dpdy = dFdy(c.position);
		float height = c.detail * detail_size * 0.15 * detail_bump * c.fade;
		float dhdx = dFdx(height);
		float dhdy = dFdy(height);
		vec3 r1 = cross(dpdy, n);
		vec3 r2 = cross(n, dpdx);
		float det = dot(dpdx, r1);
		if (abs(det) > 1e-8) {
			vec3 grad = sign(det) * (dhdx * r1 + dhdy * r2);
			n = normalize(abs(det) * n - grad);
		}
	}
	vec3 world_normal = mat3(model) * n;
	view_normal = normalize((view * vec4(world_normal, 0.0)).xyz);
}
