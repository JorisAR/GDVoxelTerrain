#[compute]
#version 450
// Accumulates the clouds JarCloudsEffect traced this frame (clouds_trace.glsl,
// half resolution, one pixel of each 2x2 block, jittered) over frames: last
// frame's result, reprojected, clamped to what this frame's neighbors allow
// (no ghosts where things changed), blended with this frame's.
// Reprojected through the body's own space: where the point seen is on the
// body now, where that was drawn last frame. Turning planets, scaled space
// and floating origin shifts follow.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0, std140) uniform Params {
	mat4 inv_projection; // the camera's, as rendered (depth 1 near, 0 far)
	mat4 camera; // camera to world
	mat4 world_to_body;
	mat4 world_to_weather; // world_to_body, then the weather's drift
	mat4 prev_view_projection;
	mat4 prev_body_to_world;
	vec4 center_scale; // the body's center as drawn, view scale
	vec4 prev_center_scale;
	vec4 layer; // bottom, top, ground (radii or heights, meters), max_distance
	vec4 up_radial; // flat: up; w: 1 radial
	vec4 pole; // spin axis in weather space
	vec4 sun_direction;
	vec4 sun_color;
	vec4 moon_direction;
	vec4 moon_color;
	vec4 ambient_light;
	vec4 cloud_color; // times brightness
	vec4 sky_light; // times sky_strength
	vec4 shape; // coverage, weather_strength, cloud_size, detail_scale
	vec4 shape2; // erosion, density, region_size, region_strength
	vec4 light; // forward, powder, weather_size (flat), night ambient
	vec4 shape_offset; // wind offset of single clouds (wrapped); w: weather ready
	vec4 detail_offset; // and of the detail; w: history valid
	vec4 flat_offset; // flat worlds: offset of the drifting weather
	vec4 quality; // steps, light_steps, history blend, -
	vec4 evolve; // meters the noise moved along the vertical: single clouds, groups, detail; faint cutoff
	vec4 weather2; // blend towards the next weather map, zonal turn of this one and the next (radians), next ready
	ivec4 sizes; // half resolution w, h; full w, h
	ivec4 rect; // half resolution pixels drawn: min x, y, max x, y (exclusive)
	ivec4 frame; // index, x and y of this frame's pixel in each 2x2 block, -
} p;

layout(set = 0, binding = 1) uniform sampler2D current_color;
layout(set = 0, binding = 2) uniform sampler2D current_depth;
layout(set = 0, binding = 3) uniform sampler2D history;
layout(rgba16f, set = 0, binding = 4) uniform restrict writeonly image2D history_next;
layout(rgba16f, set = 0, binding = 5) uniform restrict writeonly image2D out_color;

const float FAR = 1e30;

bool in_rect(ivec2 q) {
	return all(greaterThanEqual(q, p.rect.xy)) && all(lessThan(q, p.rect.zw));
}

vec4 current_at(ivec2 q) {
	q = clamp(q, ivec2(0), p.sizes.xy - 1);
	return in_rect(q) ? texelFetch(current_color, q, 0) : vec4(0.0);
}

void main() {
	// The traced rectangle and a margin (beyond it nothing is drawn).
	ivec2 low = p.sizes.xy;
	ivec2 lp = ivec2(gl_GlobalInvocationID.xy) + max(p.rect.xy - 2, ivec2(0));
	if (any(greaterThanEqual(lp, min(p.rect.zw + 2, low))))
		return;
	vec4 current = current_at(lp);
	vec4 result = current;
	if (p.detail_offset.w > 0.5 && in_rect(lp)) {
		// This frame's neighbors: their mean and spread bound the history.
		vec4 m1 = vec4(0.0);
		vec4 m2 = vec4(0.0);
		for (int y = -1; y <= 1; y++)
			for (int x = -1; x <= 1; x++) {
				vec4 c = current_at(lp + ivec2(x, y));
				m1 += c;
				m2 += c * c;
			}
		m1 /= 9.0;
		vec4 sigma = sqrt(max(m2 / 9.0 - m1 * m1, vec4(0.0)));
		vec4 lo = m1 - sigma * 1.25;
		vec4 hi = m1 + sigma * 1.25;

		// Where the clouds seen here were last frame.
		ivec2 full = p.sizes.zw;
		vec2 uv = (vec2(lp * 2) + 1.0) / vec2(full);
		vec2 ndc_xy = uv * 2.0 - 1.0;
		vec4 view = p.inv_projection * vec4(ndc_xy, 0.5, 1.0);
		vec3 dir = normalize(mat3(p.camera) * (view.xyz / view.w));
		vec2 dist = texelFetch(current_depth, lp, 0).xy;
		float d = dist.x < FAR ? dist.x : (dist.y < FAR ? dist.y : 1e7);
		vec3 world = p.camera[3].xyz + dir * d;
		vec3 body = mat3(p.world_to_body) * (world - p.center_scale.xyz) / p.center_scale.w;
		vec3 before = p.prev_center_scale.xyz + mat3(p.prev_body_to_world) * body * p.prev_center_scale.w;
		vec4 clip = p.prev_view_projection * vec4(before, 1.0);
		if (clip.w > 0.0) {
			vec2 n = clip.xy / clip.w;
			vec2 prev_uv = n * 0.5 + 0.5;
			if (all(greaterThanEqual(prev_uv, vec2(0.0))) && all(lessThanEqual(prev_uv, vec2(1.0)))) {
				vec2 hist_uv = prev_uv * vec2(full) / (2.0 * vec2(low));
				vec4 h = textureLod(history, hist_uv, 0.0);
				h = clamp(h, lo, hi);
				// Far away clouds barely move on screen: more frames, less noise.
				float meters = d / p.center_scale.w;
				result = mix(h, current, p.quality.z * mix(1.0, 0.4, smoothstep(20000.0, 300000.0, meters)));
			}
		}
	}
	imageStore(history_next, lp, result);
	imageStore(out_color, lp, result);
}
