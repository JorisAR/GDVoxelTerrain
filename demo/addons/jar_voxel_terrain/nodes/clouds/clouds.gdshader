shader_type spatial;
// Clouds for JarBodyClouds: a layer between two heights, drawn on the back
// faces of a shell around it (so it covers the screen from inside and the
// planet from outside).
//
// Where clouds are comes from the planet's weather map (JarBodyClouds makes
// it: climate zones, cyclones, fronts, cellular decks; r coverage, g how high
// the tops reach, b how sheet-like). What they look like:
// - near the camera (up to volume_distance), ray marched through 3D noise
//   (CloudNoise: billowy Perlin-Worley, eroded by finer Worley), lit through
//   the cloud towards the sun, with silver linings, approximate multiple
//   scattering and sky light;
// - beyond, and always from orbit, one look-up where the ray crosses the
//   layer: the covered fraction of the pixel and its opacity, lit with the
//   relief of the weather map's tops. Cheap, and the same weather.
// The two dissolve into each other over the last quarter of volume_distance.
// Far away (scaled space) the same, with lengths divided by view_scale.
render_mode unshaded, cull_front, depth_test_disabled, depth_draw_never, blend_premul_alpha, shadows_disabled, fog_disabled;

// Set by JarBodyClouds. Radii (radial) or heights (flat), in meters.
uniform vec3 planet_center = vec3(0.0);
uniform float cloud_bottom = 1000.0;
uniform float cloud_top = 1600.0;
uniform float ground_level = 900.0; // rays end at the sea level sphere (unless terrain is drawn first)
uniform mat3 world_to_body = mat3(1.0); // clouds turn with the body
uniform vec3 spin_axis = vec3(0.0, 1.0, 0.0); // body space
uniform vec3 up_axis = vec3(0.0, 1.0, 0.0);
uniform bool radial = true;
uniform float view_scale = 1.0; // scaled space: drawn this much smaller
uniform vec3 sun_direction = vec3(0.0, 1.0, 0.0);
uniform vec3 sun_color = vec3(1.0); // color times energy
uniform vec3 moon_direction = vec3(0.0, 1.0, 0.0);
uniform vec3 moon_color = vec3(0.0);
uniform vec3 ambient_light = vec3(0.0); // starlight (the scene's ambient floor)
uniform sampler3D cloud_noise : filter_linear_mipmap, repeat_enable;
uniform samplerCube weather_map : filter_linear_mipmap;
uniform bool weather_ready = false; // the map is made on a thread: no clouds until it is there
uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;

group_uniforms shape;
// Shifts the weather map's coverage (0.5: as it is; 0 clear, 1 overcast).
uniform float coverage : hint_range(0.0, 1.0) = 0.5;
// How much of the weather map's contrast is kept (0: the same coverage everywhere).
uniform float weather_strength : hint_range(0.0, 1.0) = 1.0;
// Flat worlds (no weather map): size of weather systems in meters, and
// (unused on planets) nothing else.
uniform float weather_size = 1500.0;
uniform float latitude_bands : hint_range(0.0, 1.0) = 0.0;
// Size of single clouds, in meters.
uniform float cloud_size = 220.0;
// Size of the eroded detail, relative to cloud_size, and how much it eats.
uniform float detail_scale : hint_range(0.02, 1.0) = 0.18;
uniform float erosion : hint_range(0.0, 1.0) = 0.4;
// How dense clouds are (extinction per meter inside a full cloud).
uniform float density = 0.03;
uniform vec3 wind = vec3(6.0, 0.0, 2.0); // meters per second
// The weather drifts around the planet: radians per hour about its axis.
uniform float weather_drift = 0.02;
group_uniforms light;
uniform vec3 cloud_color : source_color = vec3(1.0);
uniform float brightness = 1.0;
// Light from the blue sky (times the sunlight), from above.
uniform vec3 sky_light : source_color = vec3(0.38, 0.5, 0.75);
uniform float sky_strength : hint_range(0.0, 1.0) = 0.25;
// Forward scattering: bright edges with the sun behind the clouds.
uniform float forward : hint_range(0.0, 0.95) = 0.6;
group_uniforms quality;
// Ray marched up to this distance (meters); beyond, the cheap layer.
uniform float volume_distance = 30000.0;
uniform int steps : hint_range(8, 160) = 64;
uniform int light_steps : hint_range(1, 8) = 4;
// Rays stop this far (meters); flat worlds also fade out towards it.
uniform float max_distance = 60000.0;
group_uniforms;

const float NOISE_TEXELS = 64.0;

// Ray against a sphere of `radius` around the origin: (near, far), far < near if missed.
// The constant term is a product of differences: exact near the surface even
// on a planet hundreds of kilometers across.
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
float layer_height(vec3 p) {
	float h = radial ? length(p) : dot(p, up_axis);
	return (h - cloud_bottom) / (cloud_top - cloud_bottom);
}

float lod_for(float pixel, float size) {
	return max(0.0, log2(pixel / (size / NOISE_TEXELS)));
}

vec3 rotate_about(vec3 v, vec3 axis, float angle) {
	float c = cos(angle);
	float s = sin(angle);
	return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0 - c);
}

// The weather at body-space `b`: (coverage, tops, sheet-likeness).
vec3 weather(vec3 b, float pixel) {
	if (radial) {
		if (!weather_ready)
			return vec3(0.0);
		vec3 dir = rotate_about(normalize(b), normalize(spin_axis), -TIME / 3600.0 * weather_drift);
		// A cube face is 2 radii across in its 512 texels.
		float texel = 2.0 * cloud_bottom / 512.0;
		vec3 w = textureLod(weather_map, dir, max(0.0, log2(pixel / texel))).rgb;
		float c = clamp(mix(coverage, w.r + coverage - 0.5, weather_strength), 0.0, 1.0);
		return vec3(c, w.g, w.b);
	}
	// Flat worlds: drifting noise.
	vec3 q = b / weather_size;
	float lod = lod_for(pixel, weather_size);
	float w = textureLod(cloud_noise, q, lod).r * 0.6 + textureLod(cloud_noise, q * 3.1 + 0.29, lod + 1.63).r * 0.4;
	return vec3(clamp(coverage + (w - 0.5) * 2.0 * weather_strength, 0.0, 1.0), 0.6, 0.2);
}

// Height profile of the clouds (0..1 in the layer): heaped clouds rounded
// at the base and reaching up to their tops; sheets low and flat.
float profile_at(float h, vec3 w) {
	// Sparse fair-weather clouds stay flat; dense ones tower up to the map's tops.
	float tops = mix(0.3, 1.0, w.g) * mix(0.35, 1.0, smoothstep(0.2, 0.8, w.r));
	// Narrowing steadily from a flat base to the top: domes, not columns.
	float heaped = smoothstep(0.0, 0.07, h) * clamp(1.0 - pow(h / tops, 1.6), 0.0, 1.0);
	float sheet = smoothstep(0.0, 0.06, h) * (1.0 - smoothstep(0.18, 0.32, h));
	return mix(heaped, sheet, w.b);
}

// Density (extinction per meter) at body-space `b`, height `h` in the layer;
// `pixel`: meters a pixel covers there. `cheap` skips the erosion.
float cloud_density(vec3 b, float h, float pixel, bool cheap) {
	if (h <= 0.0 || h >= 1.0)
		return 0.0;
	vec3 w = weather(b, pixel);
	float c = w.r;
	if (c < 0.01)
		return 0.0;
	float profile = profile_at(h, w);
	if (profile <= 0.0)
		return 0.0;
	vec3 moved = b - wind * TIME;
	float lod = lod_for(pixel, cloud_size);
	// Sheets: larger, softer shapes.
	float n = textureLod(cloud_noise, moved / (cloud_size * (1.0 + w.b * 1.5)), lod).r;
	n = mix(n, 0.5 + (n - 0.5) * 0.6, w.b);
	float shape = clamp((n * profile - (1.0 - c)) / max(c, 0.05), 0.0, 1.0);
	// Unresolved (single clouds finer than a pixel): as opaque as the covered
	// fraction, not a fog of the average density (which is opaque throughout).
	float unresolved = smoothstep(2.0, 5.0, lod);
	if (unresolved > 0.0) {
		float fraction = smoothstep(0.22, 0.7, c);
		float column = -log(1.0 - 0.96 * fraction);
		float fog = column / ((cloud_top - cloud_bottom) * 0.45) * profile;
		return mix(shape * density, fog, unresolved);
	}
	if (shape <= 0.0)
		return 0.0;
	if (!cheap && erosion > 0.0) {
		float size = cloud_size * detail_scale;
		float detail_lod = lod_for(pixel, size);
		if (detail_lod < 5.0) {
			vec3 d = textureLod(cloud_noise, moved / size, detail_lod).gba;
			float fbm = d.x * 0.625 + d.y * 0.25 + d.z * 0.125;
			// Wispy at the bottom, billowy at the top.
			float e = erosion * (1.0 - 0.6 * w.b) * mix(1.0 - fbm, fbm, smoothstep(0.1, 0.6, h)) * (1.0 - smoothstep(3.0, 5.0, detail_lod));
			shape = clamp((shape - e * 0.5) / max(1.0 - e * 0.5, 1e-3), 0.0, 1.0);
		}
	}
	return shape * density;
}

// Sunlight reaching `p` (world orientation, real meters from the center):
// optical depth of the cloud towards the sun.
float optical_depth_to_sun(vec3 p, vec3 sun, float pixel) {
	float thickness = cloud_top - cloud_bottom;
	float step_length = thickness * 0.06;
	float od = 0.0;
	float t = 0.0;
	for (int i = 0; i < light_steps; i++) {
		t += step_length * 0.5;
		vec3 q = p + sun * t;
		od += cloud_density(world_to_body * q, layer_height(q), pixel + t * 0.05, true) * step_length;
		t += step_length * 0.5;
		step_length *= 1.8;
	}
	return od;
}

// Whether the sun is above the horizon at `p` (the planet's shadow).
float daylight_at(vec3 p, vec3 up, vec3 sun) {
	float r = radial ? length(p) : 1.0;
	float horizon = radial ? -sqrt(max(1.0 - pow(ground_level / r, 2.0), 0.0)) : -0.05;
	return smoothstep(horizon - 0.02, horizon + 0.06, dot(up, sun));
}

// The far layer at `t` along the ray (where it crosses the middle of the
// layer): premultiplied color and opacity.
vec4 far_layer(vec3 o, vec3 dir, float t, float pixel_angle, vec3 sun) {
	vec3 p = o + dir * t;
	vec3 up = radial ? normalize(p) : up_axis;
	vec3 b = world_to_body * p;
	float pixel = t * pixel_angle;
	vec3 w = weather(b, pixel);
	if (w.r < 0.01)
		return vec4(0.0);
	float thickness = cloud_top - cloud_bottom;
	// The covered part of the pixel: single clouds where they are resolved
	// (the same noise as up close), the covered fraction where they are not.
	float lod = lod_for(pixel, cloud_size);
	vec3 moved = b - wind * TIME;
	float n = textureLod(cloud_noise, moved / (cloud_size * (1.0 + w.b * 1.5)), lod).r;
	n = mix(n, 0.5 + (n - 0.5) * 0.6, w.b);
	float edge = 0.08 + 0.25 * smoothstep(1.0, 5.0, lod);
	float resolved = smoothstep(1.0 - w.r - edge, 1.0 - w.r + edge, n * mix(0.85, 1.0, w.b));
	float covered = mix(resolved, smoothstep(0.22, 0.7, w.r), smoothstep(2.0, 5.0, lod));
	// Through it: thick where tops reach high, slanted paths longer.
	float mu = radial ? abs(dot(dir, up)) : abs(dot(dir, up_axis));
	float column = density * thickness * mix(0.15, 0.45, w.g) * mix(1.0, 0.35, w.b);
	float alpha = covered * (1.0 - exp(-column / max(mu, 0.08)));
	if (alpha < 0.003)
		return vec4(0.0);
	// Relief: the tops as a height field over the surface, from the map.
	vec3 east = normalize(cross(up, abs(up.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
	vec3 north = cross(east, up);
	float step_length = max(pixel * 2.0, cloud_size * 2.0);
	float h0 = w.r * w.g;
	float he = weather(world_to_body * (p + east * step_length), pixel).r;
	float hn = weather(world_to_body * (p + north * step_length), pixel).r;
	vec3 normal = normalize(up - (east * (he - h0) + north * (hn - h0)) * thickness * 2.0 / step_length);
	float day = daylight_at(p, up, sun);
	float lambert = max(dot(normal, sun), 0.0);
	float cos_theta = dot(dir, sun);
	float scatter = (0.7 * lambert + 0.3 * max(dot(up, sun), 0.0)) * (0.85 + 0.6 * hg(forward * 0.6, cos_theta) * 4.0 * PI * 0.15);
	vec3 light = sun_color * day * scatter * 0.9
		+ (sky_light * sun_color * sky_strength * smoothstep(-0.25, 0.3, dot(up, sun)) + ambient_light)
		+ moon_color * smoothstep(-0.1, 0.2, dot(up, normalize(moon_direction))) * 0.5;
	return vec4(light * cloud_color * brightness * alpha, alpha);
}

void fragment() {
	// The ray in real meters, from the body's center (world orientation).
	vec3 camera = CAMERA_POSITION_WORLD;
	vec3 dir = normalize((INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz - camera);
	vec3 o = (camera - planet_center) / view_scale;
	float pixel_angle = length(fwidth(dir));

	float ndc = texture(depth_texture, SCREEN_UV).r;
	vec4 view = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, ndc, 1.0);
	float scene = length(view.xyz / view.w);
	vec4 far_point = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, 1e-7, 1.0);
	bool sky = ndc <= 0.0 || scene > 0.99 * length(far_point.xyz / far_point.w);
	float scene_end = sky ? 1e30 : scene / view_scale;

	// Up to two pieces of the ray inside the layer, and where it crosses the
	// middle of the layer (for the far layer).
	vec2 seg_a = vec2(1.0, -1.0);
	vec2 seg_b = vec2(1.0, -1.0);
	float mid_hit = -1.0;
	if (radial) {
		vec2 top = sphere(o, dir, cloud_top);
		if (top.y <= 0.0)
			discard;
		vec2 bottom = sphere(o, dir, cloud_bottom);
		vec2 ground = sphere(o, dir, ground_level);
		vec2 middle = sphere(o, dir, 0.5 * (cloud_bottom + cloud_top));
		float end = min(top.y, scene_end);
		if (ground.y > ground.x && ground.x > 0.0)
			end = min(end, ground.x);
		float start = max(top.x, 0.0);
		if (length(o) < cloud_bottom) {
			seg_a = vec2(max(bottom.y, 0.0), end); // from below: up through the layer
		} else if (bottom.y > bottom.x && bottom.x > 0.0) {
			seg_a = vec2(start, min(bottom.x, end)); // down to the bottom of the layer
			seg_b = vec2(bottom.y, end);             // and up again beyond, if nothing is in the way
		} else {
			seg_a = vec2(start, end);
		}
		// The first crossing of the middle ahead (the far one from inside).
		mid_hit = middle.x > 0.0 ? middle.x : middle.y;
		if (mid_hit > end)
			mid_hit = -1.0;
	} else {
		float h = dot(o, up_axis);
		float v = dot(dir, up_axis);
		float t_bottom = (cloud_bottom - h) / v;
		float t_top = (cloud_top - h) / v;
		float t0 = h > cloud_top ? t_top : h < cloud_bottom ? t_bottom : 0.0;
		float t1 = h > cloud_top ? t_bottom : h < cloud_bottom ? t_top : (v > 0.0 ? t_top : t_bottom);
		if (abs(v) < 1e-5) {
			t0 = 0.0;
			t1 = (h > cloud_bottom && h < cloud_top) ? max_distance : -1.0;
		}
		seg_a = vec2(max(t0, 0.0), min(t1, scene_end));
		float t_mid = (0.5 * (cloud_bottom + cloud_top) - h) / v;
		mid_hit = (t_mid > 0.0 && t_mid < scene_end && t_mid < max_distance) ? t_mid : -1.0;
	}
	seg_a.y = min(seg_a.y, seg_a.x + max_distance);
	seg_b.y = min(seg_b.y, seg_b.x + max_distance);

	vec3 sun = normalize(sun_direction);
	vec3 color = vec3(0.0);
	float transmittance = 1.0;

	// Near: ray marched, fading out over the last quarter of volume_distance.
	float fade_start = 0.75 * volume_distance;
	vec2 near_a = vec2(seg_a.x, min(seg_a.y, volume_distance));
	vec2 near_b = vec2(seg_b.x, min(seg_b.y, volume_distance));
	float len_a = max(near_a.y - near_a.x, 0.0);
	float len_b = max(near_b.y - near_b.x, 0.0);
	float total = len_a + len_b;
	if (total > 0.0) {
		float cos_theta = dot(dir, sun);
		// Forward scattering with a little back scattering; octaves of multiple
		// scattering spread the light out (flatter phase, less extinction).
		float phase0 = mix(hg(forward, cos_theta), hg(-0.3, cos_theta), 0.25) * 4.0 * PI;
		float phase1 = mix(hg(forward * 0.5, cos_theta), hg(-0.15, cos_theta), 0.25) * 4.0 * PI;
		// Per pixel, unpatterned (a gradient-noise jitter showed as hatching).
		uvec2 px = uvec2(FRAGCOORD.xy);
		uint hj = px.x * 1973u + px.y * 9277u + 26699u;
		hj = (hj ^ (hj >> 13u)) * 1274126177u;
		float jitter = float((hj ^ (hj >> 16u)) & 65535u) / 65536.0;
		int count = steps;
		// Steps short near the camera, longer further away (quadratic spacing).
		for (int i = 0; i < count; i++) {
			float u = (float(i) + jitter) / float(count);
			float x = u * u * total;
			float dt = 2.0 * u * total / float(count) + total / float(count * count);
			float t = x < len_a ? near_a.x + x : near_b.x + (x - len_a);
			vec3 p = o + dir * t;
			float h = layer_height(p);
			float pixel = t * pixel_angle;
			float d = cloud_density(world_to_body * p, h, pixel, false) * (1.0 - smoothstep(fade_start, volume_distance, t));
			if (d <= 0.0)
				continue;
			vec3 up = radial ? normalize(p) : up_axis;
			float daylight = daylight_at(p, up, sun);
			vec3 direct = vec3(0.0);
			if (daylight > 0.0) {
				float od = optical_depth_to_sun(p, sun, pixel);
				// Octaves of 1, 1/2 and 1/4: normalized (/ 1.75) so a cloud's lit
				// edge sends back about what it receives, as real clouds do.
				float scatter = (exp(-od) * phase0 + 0.5 * exp(-od * 0.25) * phase1 + 0.25 * exp(-od * 0.0625)) / 1.75;
				direct = sun_color * daylight * scatter;
			}
			float day = smoothstep(-0.25, 0.3, dot(up, sun));
			vec3 ambient = (sky_light * sun_color * sky_strength * day + ambient_light) * mix(0.45, 1.0, clamp(h, 0.0, 1.0));
			vec3 moon = moon_color * smoothstep(-0.1, 0.2, dot(up, normalize(moon_direction))) * mix(0.3, 1.0, h) * 0.6;
			float step_transmittance = exp(-d * dt);
			color += transmittance * (direct + ambient + moon) * (1.0 - step_transmittance);
			transmittance *= step_transmittance;
			if (transmittance < 0.01)
				break;
		}
		color *= cloud_color * brightness;
	}

	// Far: the cheap layer where the ray crosses it beyond the near part.
	if (mid_hit > fade_start && transmittance > 0.01) {
		vec4 layer = far_layer(o, dir, mid_hit, pixel_angle, sun) * smoothstep(fade_start, volume_distance, mid_hit);
		color += transmittance * layer.rgb;
		transmittance *= 1.0 - layer.a;
	}

	// Premultiplied: the light the clouds send, and how much they hide.
	float fade = radial ? 1.0 : 1.0 - smoothstep(max_distance * 0.6, max_distance, seg_a.x);
	float alpha = (1.0 - transmittance) * fade;
	if (alpha < 0.003)
		discard;
	ALBEDO = color * fade;
	ALPHA = alpha;
}
