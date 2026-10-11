shader_type spatial;
// JarVoxelVolume terrain shader.
//
// Layers: 0-7 are the paintable materials (edits and JarVoxelVolume.paint() write
// their weights into the mesh), 8-10 cover unpainted ground by rule:
//   8 top (flat ground), 9 cliff (steep ground), 10 shore (near sea level).
// Every layer has a tint, texture scale and roughness. With texture arrays, layer
// i samples array layer i (layers the arrays do not have use the tint alone).
//
// Everything is computed in the volume's own space: textures stay put on a
// moving volume, and "up" follows the volume's up_mode (axis or radial), set
// per chunk through instance uniforms by JarVoxelVolume.

instance uniform vec3 jar_chunk_origin;          // chunk center in the volume
instance uniform vec4 jar_up_center = vec4(0.0);  // xyz: up center, w: sea level
instance uniform vec4 jar_up_axis = vec4(0.0, 1.0, 0.0, 0.0); // xyz: up axis, w: mode (0 axis, 1 radial)

group_uniforms textures;
uniform sampler2DArray albedo_textures : source_color, filter_linear_mipmap_anisotropic, repeat_enable;
uniform sampler2DArray normal_textures : hint_normal, filter_linear_mipmap_anisotropic, repeat_enable;
uniform bool use_albedo_textures = false;
uniform bool use_normal_textures = false;
uniform float normal_strength : hint_range(0.0, 2.0) = 1.0;
// Higher values give sharper transitions between the three projections.
uniform float triplanar_sharpness : hint_range(1.0, 16.0) = 4.0;

group_uniforms rules;
// Ground steeper than this (degrees from up) uses the cliff layer.
uniform float cliff_angle : hint_range(0.0, 90.0) = 40.0;
uniform float cliff_blend : hint_range(0.0, 30.0) = 8.0;
// Ground below this height (meters above sea level) uses the shore layer.
uniform float shore_height = 2.0;
uniform float shore_blend : hint_range(0.0, 50.0) = 2.0;

group_uniforms painted;
uniform vec4 material_0_tint : source_color = vec4(0.45, 0.30, 0.18, 1.0); // dirt
uniform vec4 material_1_tint : source_color = vec4(0.42, 0.44, 0.48, 1.0); // stone
uniform vec4 material_2_tint : source_color = vec4(0.85, 0.65, 0.15, 1.0); // ore
uniform vec4 material_3_tint : source_color = vec4(0.55, 0.25, 0.85, 1.0); // crystal
uniform vec4 material_4_tint : source_color = vec4(0.80, 0.80, 0.78, 1.0); // concrete
uniform vec4 material_5_tint : source_color = vec4(0.62, 0.20, 0.15, 1.0); // brick
uniform vec4 material_6_tint : source_color = vec4(0.90, 0.92, 0.95, 1.0); // snow
uniform vec4 material_7_tint : source_color = vec4(0.10, 0.10, 0.12, 1.0); // basalt
uniform vec4 material_roughness_0_3 = vec4(0.9, 0.8, 0.3, 0.15);
uniform vec4 material_roughness_4_7 = vec4(0.85, 0.8, 0.6, 0.7);
uniform vec4 material_scale_0_3 = vec4(0.25);
uniform vec4 material_scale_4_7 = vec4(0.25);

group_uniforms ground;
uniform vec4 top_tint : source_color = vec4(0.26, 0.38, 0.23, 1.0);
uniform vec4 cliff_tint : source_color = vec4(0.61, 0.67, 0.70, 1.0);
uniform vec4 shore_tint : source_color = vec4(0.79, 0.72, 0.56, 1.0);
uniform vec3 ground_roughness = vec3(0.85, 0.8, 0.9); // top, cliff, shore
uniform vec3 ground_scale = vec3(0.25); // top, cliff, shore
group_uniforms;

varying vec3 v_position; // volume space
varying vec3 v_normal;   // volume space
varying vec4 v_materials_high;

void vertex() {
	v_position = jar_chunk_origin + VERTEX;
	v_normal = NORMAL;
	v_materials_high = CUSTOM0;
}

vec3 triplanar_weights(vec3 n) {
	vec3 w = pow(abs(n), vec3(triplanar_sharpness));
	return w / max(w.x + w.y + w.z, 1e-5);
}

vec4 sample_albedo(int layer, vec3 p, vec3 w, float scale) {
	if (!use_albedo_textures || layer >= textureSize(albedo_textures, 0).z)
		return vec4(1.0);
	vec3 q = p * scale;
	float l = float(layer);
	return texture(albedo_textures, vec3(q.zy, l)) * w.x
		+ texture(albedo_textures, vec3(q.xz, l)) * w.y
		+ texture(albedo_textures, vec3(q.xy, l)) * w.z;
}

// Whiteout blend of the three projected tangent-space normals (volume space out).
vec3 sample_normal(int layer, vec3 p, vec3 n, vec3 w, float scale) {
	if (!use_normal_textures || layer >= textureSize(normal_textures, 0).z)
		return n;
	vec3 q = p * scale;
	float l = float(layer);
	vec2 tx = (texture(normal_textures, vec3(q.zy, l)).xy * 2.0 - 1.0) * normal_strength;
	vec2 ty = (texture(normal_textures, vec3(q.xz, l)).xy * 2.0 - 1.0) * normal_strength;
	vec2 tz = (texture(normal_textures, vec3(q.xy, l)).xy * 2.0 - 1.0) * normal_strength;
	vec3 nx = vec3(tx + n.zy, abs(n.x) * sign(n.x));
	vec3 ny = vec3(ty + n.xz, abs(n.y) * sign(n.y));
	vec3 nz = vec3(tz + n.xy, abs(n.z) * sign(n.z));
	return normalize(nx.zyx * w.x + ny.xzy * w.y + nz.xyz * w.z);
}

void fragment() {
	vec3 n = normalize(v_normal);
	vec3 up = jar_up_axis.w > 0.5 ? normalize(v_position - jar_up_center.xyz) : jar_up_axis.xyz;
	float height = jar_up_axis.w > 0.5 ? length(v_position - jar_up_center.xyz) - jar_up_center.w
		: dot(v_position - jar_up_center.xyz, jar_up_axis.xyz) - jar_up_center.w;

	// Layer weights: painted materials first, the rules fill the rest.
	float weights[11];
	vec4 lo = COLOR;
	vec4 hi = v_materials_high;
	weights[0] = lo.r; weights[1] = lo.g; weights[2] = lo.b; weights[3] = lo.a;
	weights[4] = hi.r; weights[5] = hi.g; weights[6] = hi.b; weights[7] = hi.a;
	float painted = lo.r + lo.g + lo.b + lo.a + hi.r + hi.g + hi.b + hi.a;
	float scale_painted = painted > 1.0 ? 1.0 / painted : 1.0;
	for (int i = 0; i < 8; i++)
		weights[i] *= scale_painted;
	float ground = 1.0 - clamp(painted, 0.0, 1.0);
	float cliff_cos = cos(radians(cliff_angle));
	float blend_cos = sin(radians(cliff_angle)) * radians(cliff_blend) * 0.5;
	float cliff = 1.0 - smoothstep(cliff_cos - blend_cos, cliff_cos + blend_cos, dot(n, up));
	float shore = (1.0 - smoothstep(shore_height - shore_blend, shore_height + shore_blend, height)) * (1.0 - cliff);
	weights[8] = ground * (1.0 - cliff - shore);
	weights[9] = ground * cliff;
	weights[10] = ground * shore;

	vec4 tints[11] = vec4[](material_0_tint, material_1_tint, material_2_tint, material_3_tint,
		material_4_tint, material_5_tint, material_6_tint, material_7_tint, top_tint, cliff_tint, shore_tint);
	float roughness[11] = float[](material_roughness_0_3.x, material_roughness_0_3.y, material_roughness_0_3.z,
		material_roughness_0_3.w, material_roughness_4_7.x, material_roughness_4_7.y, material_roughness_4_7.z,
		material_roughness_4_7.w, ground_roughness.x, ground_roughness.y, ground_roughness.z);
	float scales[11] = float[](material_scale_0_3.x, material_scale_0_3.y, material_scale_0_3.z, material_scale_0_3.w,
		material_scale_4_7.x, material_scale_4_7.y, material_scale_4_7.z, material_scale_4_7.w,
		ground_scale.x, ground_scale.y, ground_scale.z);

	vec3 w = triplanar_weights(n);
	vec3 albedo = vec3(0.0);
	vec3 normal = vec3(0.0);
	float rough = 0.0;
	float total = 0.0;
	for (int i = 0; i < 11; i++) {
		float k = weights[i];
		if (k < 0.01)
			continue; // most fragments blend one or two layers
		albedo += k * tints[i].rgb * sample_albedo(i, v_position, w, scales[i]).rgb;
		normal += k * sample_normal(i, v_position, n, w, scales[i]);
		rough += k * roughness[i];
		total += k;
	}
	total = max(total, 1e-4);
	ALBEDO = albedo / total;
	ROUGHNESS = rough / total;
	SPECULAR = 0.2;
	vec3 world_normal = mat3(MODEL_MATRIX) * normalize(normal / total);
	NORMAL = normalize((VIEW_MATRIX * vec4(world_normal, 0.0)).xyz);
}
