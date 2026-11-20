#[compute]
#version 450
// Clouds for JarBodyClouds, traced at half resolution (one pixel of each 2x2
// block per frame, a different one each frame; JarCloudsEffect accumulates
// them over frames). One model at every distance: the same density and the
// same light from inside a cloud to the whole planet seen from afar. Only the
// detail that is smaller than a pixel drops out, as its average.
//
// Scales, each modulating the one below:
// - the weather map (the planet's climate: coverage, how high tops reach, how
//   sheet-like), a cube map;
// - regions (region_size, smooth noise stretched east-west): clusters, gaps
//   and streets inside the weather;
// - single clouds (cloud_size: Perlin-Worley, 4 clouds across a period);
// - detail (cloud_size * detail_scale: Worley erosion).
// Where single clouds are smaller than a pixel, the layer is as opaque as the
// covered fraction of the pixel (not a fog of the mean density, which would
// be opaque throughout).
//
// Out: premultiplied color and opacity, and (front, end): the distance of
// the clouds seen (weighted by what they add) and where the ray ended, both
// in render units, for the upsampling.

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
	vec4 light; // forward, powder, weather_size (flat), -
	vec4 shape_offset; // wind offset of single clouds (wrapped); w: weather ready
	vec4 detail_offset; // and of the detail; w: history valid
	vec4 flat_offset; // flat worlds: offset of the drifting weather
	vec4 quality; // steps, light_steps, history blend, -
	ivec4 sizes; // half resolution w, h; full w, h
	ivec4 rect; // half resolution pixels drawn: min x, y, max x, y (exclusive)
	ivec4 frame; // index, x and y of this frame's pixel in each 2x2 block, -
} p;

layout(set = 0, binding = 1) uniform sampler3D shape_noise;
layout(set = 0, binding = 2) uniform sampler3D detail_noise;
layout(set = 0, binding = 3) uniform samplerCube weather_map;
layout(set = 0, binding = 4) uniform sampler2D depth_buffer;
layout(rgba16f, set = 0, binding = 5) uniform restrict writeonly image2D out_color;
layout(rg32f, set = 0, binding = 6) uniform restrict writeonly image2D out_depth;

const float PI = 3.14159265359;
const float SHAPE_TEXELS = 128.0;
const float DETAIL_TEXELS = 64.0;
const float WEATHER_TEXELS = 512.0;
const float FAR = 1e30;

bool radial() {
	return p.up_radial.w > 0.5;
}

float cloud_bottom() {
	return p.layer.x;
}

float cloud_top() {
	return p.layer.y;
}

// Ray against a sphere of `radius` around the origin: (near, far), far < near
// if missed. The constant term as a product of differences: exact near the
// surface even on a planet hundreds of kilometers across.
vec2 sphere(vec3 o, vec3 d, float radius) {
	float r = length(o);
	float b = dot(o, d);
	float c = (r - radius) * (r + radius);
	float h = b * b - c;
	if (h < 0.0)
		return vec2(1.0, -1.0);
	h = sqrt(h);
	return vec2(-b - h, -b + h);
}

float hg(float g, float cos_theta) {
	float d = 1.0 + g * g - 2.0 * g * cos_theta;
	return (1.0 - g * g) / (4.0 * PI * d * sqrt(max(d, 1e-6)));
}

// Height in the layer, 0 at the bottom, 1 at the top.
float layer_height(vec3 x) {
	float h = radial() ? length(x) : dot(x, p.up_radial.xyz);
	return (h - cloud_bottom()) / (cloud_top() - cloud_bottom());
}

// Mip level for a texture of `texels` per `period` meters seen with `pixel`
// meters a pixel.
float lod_for(float pixel, float period, float texels) {
	return max(0.0, log2(pixel / (period / texels)));
}

float remap01(float v, float lo, float hi) {
	return clamp((v - lo) / max(hi - lo, 1e-4), 0.0, 1.0);
}

struct Weather {
	float coverage; // after the regions
	float tops; // how high the tops reach
	float sheet; // how sheet-like
	float warp; // the regions' offset of single clouds, in cloud sizes
	float spread; // coverage variation lost to averaging (unresolved regions)
};

// The weather at world-orientation `x` (meters from the body's center).
Weather weather_at(vec3 x, float pixel) {
	Weather w;
	w.tops = 0.6;
	w.sheet = 0.2;
	w.warp = 0.0;
	w.spread = 0.0;
	float c;
	vec3 q;
	if (radial()) {
		if (p.shape_offset.w < 0.5) {
			w.coverage = 0.0;
			return w;
		}
		q = mat3(p.world_to_weather) * x;
		// A cube face is 2 radii across in its texels.
		float texel = 2.0 * cloud_bottom() / WEATHER_TEXELS;
		vec3 m = textureLod(weather_map, q, log2(max(pixel / texel, 1.0))).rgb;
		c = clamp(mix(p.shape.x, m.r + p.shape.x - 0.5, p.shape.y), 0.0, 1.0);
		w.tops = m.g;
		w.sheet = m.b;
	} else {
		// Flat worlds: drifting noise for the weather.
		q = mat3(p.world_to_body) * x;
		vec3 s = (q + p.flat_offset.xyz) / p.light.z;
		float lod = lod_for(pixel, p.light.z, SHAPE_TEXELS);
		float n = textureLod(shape_noise, s, lod).g * 0.6 + textureLod(shape_noise, s * 2.7 + 0.29, lod + 1.43).g * 0.4;
		c = clamp(p.shape.x + (n - 0.5) * 2.0 * p.shape.y, 0.0, 1.0);
	}
	// Regions: smooth noise (fbm), stretched east-west (latitude squeezed) as
	// zonal winds draw weather out. Clear skies stay clear (and skip it).
	float strength = p.shape2.w;
	if (strength > 0.0 && c > 0.02) {
		float size = p.shape2.z;
		vec3 axis = radial() ? p.pole.xyz : p.up_radial.xyz;
		vec3 r = q + axis * dot(q, axis) * 1.2;
		float lod_a = lod_for(pixel, size, SHAPE_TEXELS);
		float n = (textureLod(shape_noise, r / size, lod_a).g - 0.5) * 1.15;
		// Mipmaps average the noise out where it is smaller than a pixel: the
		// variation that is lost, to widen the covered fraction instead.
		float kept = 1.0 - smoothstep(3.5, 6.5, lod_a);
		w.spread = strength * 0.35 * (1.0 - kept);
		// Clusters and gaps where there are clouds; few new ones in the clear.
		float d = n * 2.4 * strength;
		c = clamp(c + d * mix(0.25, 1.0, smoothstep(0.05, 0.35, c)), 0.0, 1.0);
		w.warp = n * 1.5;
	}
	w.coverage = c;
	return w;
}

// Height profile of the clouds (0..1 in the layer): heaped clouds rounded
// at the base and reaching up to their tops; sheets low and flat.
float profile_at(float h, Weather w) {
	// Sparse fair-weather clouds stay flat; dense ones tower up to the map's tops.
	float tops = mix(0.3, 1.0, w.tops) * mix(0.35, 1.0, smoothstep(0.2, 0.8, w.coverage));
	// Narrowing steadily from a flat base to the top: domes, not columns.
	float heaped = smoothstep(0.0, 0.07, h) * clamp(1.0 - pow(max(h / tops, 0.0), 1.6), 0.0, 1.0);
	float sheet = smoothstep(0.0, 0.06, h) * (1.0 - smoothstep(0.18, 0.32, h));
	return mix(heaped, sheet, w.sheet);
}

// Density (extinction per meter) at world-orientation `x` (meters from the
// body's center) in weather `w`; `pixel`: meters a pixel covers there.
// `cheap` skips the erosion (light paths).
float cloud_density_in(vec3 x, Weather w, float pixel, bool cheap) {
	float h = layer_height(x);
	if (h <= 0.0 || h >= 1.0)
		return 0.0;
	float c = w.coverage;
	if (c < 0.01 && w.spread < 0.01)
		return 0.0;
	float profile = profile_at(h, w);
	if (profile <= 0.0)
		return 0.0;
	float size = p.shape.z;
	float density = p.shape2.y;
	// Single clouds: resolved while a cloud (a quarter period) covers pixels.
	float unresolved = smoothstep(-3.0, 0.0, log2(pixel / (size * 0.25)));
	float shape = 0.0;
	if (unresolved < 1.0) {
		vec3 b = mat3(p.world_to_body) * x;
		// Sheets: larger, softer shapes.
		float period = size * (1.0 + w.sheet * 1.5);
		vec3 s = (b - p.shape_offset.xyz) / period + w.warp * 0.35;
		float n = textureLod(shape_noise, s, lod_for(pixel, period, SHAPE_TEXELS)).r;
		n = mix(n, 0.5 + (n - 0.5) * 0.6, w.sheet);
		shape = clamp((n * profile - (1.0 - c)) / max(c, 0.05), 0.0, 1.0);
		if (shape > 0.0 && !cheap && p.shape2.x > 0.0) {
			float dsize = size * p.shape.w;
			float dlod = lod_for(pixel, dsize, DETAIL_TEXELS);
			if (dlod < 5.0) {
				vec3 d = textureLod(detail_noise, (b - p.detail_offset.xyz) / dsize, dlod).rgb;
				float fbm = d.x * 0.625 + d.y * 0.25 + d.z * 0.125;
				// Wispy at the base, billowy above it.
				float e = p.shape2.x * (1.0 - 0.6 * w.sheet) * mix(1.0 - fbm, fbm, smoothstep(0.04, 0.2, h)) * (1.0 - smoothstep(3.0, 5.0, dlod));
				shape = clamp((shape - e * 0.5) / max(1.0 - e * 0.5, 1e-3), 0.0, 1.0);
			}
		}
	}
	shape *= density;
	if (unresolved > 0.0) {
		// As opaque through the layer as the covered fraction of the pixel.
		float spread = w.spread + 0.04;
		float fraction = smoothstep(0.22 - spread, 0.7 + spread, c);
		float column = -log(1.0 - 0.96 * fraction);
		float fog = column / ((cloud_top() - cloud_bottom()) * 0.45) * profile;
		shape = mix(shape, fog, unresolved);
	}
	return shape;
}

// Optical depth of the cloud from `x` towards the sun, in the weather there
// (the path is a few kilometers: the weather and regions barely change).
float optical_depth_to_sun(vec3 x, Weather w, vec3 sun, float pixel) {
	float thickness = cloud_top() - cloud_bottom();
	float step_length = thickness * 0.06;
	float od = 0.0;
	float t = 0.0;
	int count = int(p.quality.y);
	for (int i = 0; i < count; i++) {
		t += step_length * 0.5;
		od += cloud_density_in(x + sun * t, w, pixel + t * 0.05, true) * step_length;
		t += step_length * 0.5;
		step_length *= 1.8;
	}
	return od;
}

// Whether the sun is above the horizon at `x` (the planet's shadow).
float daylight_at(vec3 x, vec3 up, vec3 sun) {
	float r = radial() ? length(x) : 1.0;
	float horizon = radial() ? -sqrt(max(1.0 - pow(p.layer.z / r, 2.0), 0.0)) : -0.05;
	return smoothstep(horizon - 0.02, horizon + 0.06, dot(up, sun));
}

float hash(uvec3 v) {
	uint h = v.x * 1973u + v.y * 9277u + v.z * 26699u + 7u;
	h = (h ^ (h >> 13u)) * 1274126177u;
	h ^= h >> 16u;
	return float(h & 65535u) / 65536.0;
}

// Linear distance (render units) of a depth buffer value along the ray
// through `ndc_xy`; FAR for the sky.
float scene_distance(vec2 ndc_xy, float depth) {
	if (depth <= 0.0)
		return FAR;
	vec4 v = p.inv_projection * vec4(ndc_xy, depth, 1.0);
	return length(v.xyz / v.w);
}

void main() {
	ivec2 lp = ivec2(gl_GlobalInvocationID.xy) + p.rect.xy;
	if (any(greaterThanEqual(lp, p.rect.zw)))
		return;
	ivec2 full = p.sizes.zw;
	// This frame's pixel of the block; the ray ends at the block's nearest
	// surface (depth 1 near): no cloud is ever drawn over something in front
	// of it. Where the block also holds farther pixels (an edge against the
	// sky), the upsampling prefers neighbors that reached as far.
	ivec2 fp = min(lp * 2 + p.frame.yz, full - 1);
	float depth = 0.0;
	for (int i = 0; i < 4; i++)
		depth = max(depth, texelFetch(depth_buffer, min(lp * 2 + ivec2(i & 1, i >> 1), full - 1), 0).r);
	vec2 uv = (vec2(fp) + 0.5) / vec2(full);
	vec2 ndc_xy = uv * 2.0 - 1.0;
	vec4 view = p.inv_projection * vec4(ndc_xy, 0.5, 1.0);
	vec3 dir = normalize(mat3(p.camera) * (view.xyz / view.w));
	float scene = scene_distance(ndc_xy, depth);

	float view_scale = p.center_scale.w;
	vec3 o = (p.camera[3].xyz - p.center_scale.xyz) / view_scale;
	float scene_end = scene >= FAR ? FAR : scene / view_scale;
	// Meters per pixel per meter of distance (at full resolution).
	float pixel_angle = 2.0 * abs(p.inv_projection[1][1]) / float(full.y);

	// Up to two pieces of the ray inside the layer.
	vec2 seg_a = vec2(1.0, -1.0);
	vec2 seg_b = vec2(1.0, -1.0);
	bool inside = false;
	if (radial()) {
		vec2 top = sphere(o, dir, cloud_top());
		if (top.y > 0.0) {
			vec2 bottom = sphere(o, dir, cloud_bottom());
			vec2 ground = sphere(o, dir, p.layer.z);
			float end = min(top.y, scene_end);
			if (ground.y > ground.x && ground.x > 0.0)
				end = min(end, ground.x);
			float start = max(top.x, 0.0);
			float r = length(o);
			inside = r < cloud_top() && r > cloud_bottom();
			if (r < cloud_bottom()) {
				seg_a = vec2(max(bottom.y, 0.0), end); // from below: up through the layer
			} else if (bottom.y > bottom.x && bottom.x > 0.0) {
				seg_a = vec2(start, min(bottom.x, end)); // down to the bottom of the layer
				seg_b = vec2(bottom.y, end); // and up again beyond, if nothing is in the way
			} else {
				seg_a = vec2(start, end);
			}
		}
	} else {
		vec3 up = p.up_radial.xyz;
		float h = dot(o, up);
		float v = dot(dir, up);
		inside = h > cloud_bottom() && h < cloud_top();
		float t0 = 0.0;
		float t1 = -1.0;
		if (abs(v) < 1e-5) {
			t1 = inside ? p.layer.w : -1.0;
		} else {
			float t_bottom = (cloud_bottom() - h) / v;
			float t_top = (cloud_top() - h) / v;
			t0 = h > cloud_top() ? t_top : h < cloud_bottom() ? t_bottom : 0.0;
			t1 = h > cloud_top() ? t_bottom : h < cloud_bottom() ? t_top : (v > 0.0 ? t_top : t_bottom);
		}
		seg_a = vec2(max(t0, 0.0), min(t1, scene_end));
	}
	seg_a.y = min(seg_a.y, seg_a.x + p.layer.w);
	seg_b.y = min(seg_b.y, seg_b.x + p.layer.w);
	float len_a = max(seg_a.y - seg_a.x, 0.0);
	float len_b = max(seg_b.y - seg_b.x, 0.0);
	float total = len_a + len_b;

	vec3 sun = normalize(p.sun_direction.xyz);
	vec3 color = vec3(0.0);
	float transmittance = 1.0;
	float front_sum = 0.0;
	float front_weight = 0.0;
	if (total > 0.0) {
		float cos_theta = dot(dir, sun);
		// Forward scattering with a little back scattering; octaves of multiple
		// scattering spread the light out (flatter phase, less extinction).
		float forward = p.light.x;
		float phase0 = mix(hg(forward, cos_theta), hg(-0.3, cos_theta), 0.25) * 4.0 * PI;
		float phase1 = mix(hg(forward * 0.5, cos_theta), hg(-0.15, cos_theta), 0.25) * 4.0 * PI;
		// Dark edges facing away from the sun (in-scattering builds up inside a
		// cloud); none looking towards the sun.
		float powder_amount = p.light.y * (0.5 - 0.5 * cos_theta);
		vec3 moon_dir = normalize(p.moon_direction.xyz);
		// Steps: as many as the path needs for what a pixel resolves there
		// (few from orbit), evenly spaced while they cover the path (thin low
		// clouds need even steps); longer paths near the camera get the
		// steps short near it and longer further away.
		float mid_pixel = (seg_a.x + total * 0.5) * pixel_angle;
		float feature = max(mid_pixel, p.shape.z * 0.03);
		int max_steps = int(p.quality.x);
		float wanted = total / feature;
		int count = clamp(int(ceil(wanted)), min(12, max_steps), max_steps);
		bool near = inside || seg_a.x < (cloud_top() - cloud_bottom());
		float near_weight = near ? smoothstep(float(max_steps), float(max_steps) * 2.0, wanted) : 0.0;
		float jitter = hash(uvec3(uvec2(lp), uint(p.frame.x)));
		jitter = fract(jitter + float(p.frame.x & 63) * 0.618034);
		for (int i = 0; i < count; i++) {
			float u0 = float(i) / float(count);
			float u1 = float(i + 1) / float(count);
			float u = (float(i) + jitter) / float(count);
			// Quadratic spacing near the camera, even from outside.
			float x = mix(u, u * u, near_weight) * total;
			float dt = (mix(u1, u1 * u1, near_weight) - mix(u0, u0 * u0, near_weight)) * total;
			float t = x < len_a ? seg_a.x + x : seg_b.x + (x - len_a);
			vec3 pos = o + dir * t;
			float pixel = t * pixel_angle;
			float hl = layer_height(pos);
			if (hl <= 0.0 || hl >= 1.0)
				continue;
			Weather w = weather_at(pos, pixel);
			float d = cloud_density_in(pos, w, pixel, false);
			if (d <= 0.0)
				continue;
			float h = clamp(layer_height(pos), 0.0, 1.0);
			vec3 up = radial() ? normalize(pos) : p.up_radial.xyz;
			float daylight = daylight_at(pos, up, sun);
			vec3 direct = vec3(0.0);
			if (daylight > 0.0) {
				float od = optical_depth_to_sun(pos, w, sun, pixel);
				// Octaves of 1, 1/2 and 1/4: normalized (/ 1.75) so a cloud's lit
				// edge sends back about what it receives, as real clouds do.
				float scatter = (exp(-od) * phase0 + 0.5 * exp(-od * 0.25) * phase1 + 0.25 * exp(-od * 0.0625)) / 1.75;
				float powder = 1.0 - exp(-d * (cloud_top() - cloud_bottom()) * 0.25 - od * 2.0);
				scatter *= mix(1.0, 0.35 + 0.65 * powder, powder_amount);
				direct = p.sun_color.rgb * daylight * scatter;
			}
			float day = smoothstep(-0.25, 0.3, dot(up, sun));
			vec3 ambient = (p.sky_light.rgb * p.sun_color.rgb * day + p.ambient_light.rgb) * mix(0.45, 1.0, h);
			vec3 moon = p.moon_color.rgb * smoothstep(-0.1, 0.2, dot(up, moon_dir)) * mix(0.3, 1.0, h) * 0.6;
			float step_transmittance = exp(-d * dt);
			float added = transmittance * (1.0 - step_transmittance);
			color += added * (direct + ambient + moon);
			front_sum += added * t;
			front_weight += added;
			transmittance *= step_transmittance;
			if (transmittance < 0.01)
				break;
		}
		color *= p.cloud_color.rgb;
	}

	// Flat worlds fade out towards max_distance.
	float fade = radial() ? 1.0 : 1.0 - smoothstep(p.layer.w * 0.6, p.layer.w, seg_a.x);
	float alpha = (1.0 - transmittance) * fade;
	float front = front_weight > 1e-4 ? front_sum / front_weight * view_scale : (total > 0.0 ? seg_a.x * view_scale : FAR);
	imageStore(out_color, lp, vec4(color * fade, alpha));
	imageStore(out_depth, lp, vec4(front, scene, 0.0, 0.0));
}
