shader_type spatial;
// Volumetric clouds for JarBodyClouds: a layer between two heights, ray
// marched per pixel (drawn on the back faces of a shell around it, so it
// covers the screen from inside and the planet from outside).
//
// Density: a weather field (coverage over the planet: weather systems, and
// optionally climate bands by latitude) decides where clouds are; billowy 3D
// noise (CloudNoise, Perlin-Worley) shapes them, rounded at the base and
// tapering at the top, and finer Worley noise erodes their edges. Lit by the
// sun through the cloud above each sample (a short march towards it), with
// forward scattering (silver linings), multiple scattering approximated by
// octaves (Wrenninge), and the sky's light from above.
//
// One function at every distance: noise finer than a pixel fades to its
// average (mipmaps), so the layer seen from orbit is the same clouds blurred,
// not a different texture. Far away (scaled space) the same, with lengths
// divided by view_scale.
render_mode unshaded, cull_front, depth_test_disabled, depth_draw_never, blend_premul_alpha, shadows_disabled, fog_disabled;

// Set by JarBodyClouds. Radii (radial) or heights (flat), in meters.
uniform vec3 planet_center = vec3(0.0);
uniform float cloud_bottom = 1000.0;
uniform float cloud_top = 1600.0;
uniform float ground_level = 900.0; // rays end at the sea level sphere (unless terrain is drawn first)
uniform mat3 world_to_body = mat3(1.0); // clouds turn with the body
uniform vec3 spin_axis = vec3(0.0, 1.0, 0.0); // body space, for latitude
uniform vec3 up_axis = vec3(0.0, 1.0, 0.0);
uniform bool radial = true;
uniform float view_scale = 1.0; // scaled space: drawn this much smaller
uniform vec3 sun_direction = vec3(0.0, 1.0, 0.0);
uniform vec3 sun_color = vec3(1.0); // color times energy
uniform vec3 moon_direction = vec3(0.0, 1.0, 0.0);
uniform vec3 moon_color = vec3(0.0);
uniform vec3 ambient_light = vec3(0.0); // starlight (the scene's ambient floor)
uniform sampler3D cloud_noise : filter_linear_mipmap, repeat_enable;
uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;

group_uniforms shape;
// Fraction of the sky covered, on average.
uniform float coverage : hint_range(0.0, 1.0) = 0.35;
// Weather: coverage varies by this much, over systems `weather_size` meters
// across, so there are clear and overcast regions.
uniform float weather_strength : hint_range(0.0, 1.0) = 0.6;
uniform float weather_size = 1500.0;
// Climate by latitude: cloudy equator and storm tracks, clear subtropics.
uniform float latitude_bands : hint_range(0.0, 1.0) = 0.0;
// Size of single clouds, in meters.
uniform float cloud_size = 220.0;
// Size of the eroded detail, relative to cloud_size, and how much it eats.
uniform float detail_scale : hint_range(0.02, 1.0) = 0.18;
uniform float erosion : hint_range(0.0, 1.0) = 0.4;
// How dense clouds are (extinction per meter inside a full cloud).
uniform float density = 0.03;
uniform vec3 wind = vec3(6.0, 0.0, 2.0); // meters per second
group_uniforms light;
uniform vec3 cloud_color : source_color = vec3(1.0);
uniform float brightness = 1.0;
// Light from the blue sky (times the sunlight), from above.
uniform vec3 sky_light : source_color = vec3(0.38, 0.5, 0.75);
uniform float sky_strength : hint_range(0.0, 1.0) = 0.25;
// Forward scattering: bright edges with the sun behind the clouds.
uniform float forward : hint_range(0.0, 0.95) = 0.6;
group_uniforms quality;
uniform int steps : hint_range(8, 160) = 96;
uniform int light_steps : hint_range(1, 8) = 5;
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

// Coverage at body-space position `b` (weather systems, latitude).
float weather(vec3 b, float pixel) {
	vec3 q = b / weather_size;
	float lod = lod_for(pixel, weather_size);
	vec3 warp = textureLod(cloud_noise, q * 0.21 + 0.37, max(lod - 2.2, 0.0)).gba - 0.5;
	vec3 qw = q + warp * 0.8;
	// Systems, and inside them clusters and gaps a tenth their size (what
	// gives cloud fields their look from orbit), sharpened.
	float w = textureLod(cloud_noise, qw, lod).r * 0.5
		+ textureLod(cloud_noise, qw * 3.1 + 0.29, lod + 1.63).r * 0.3
		+ textureLod(cloud_noise, qw * 9.7 + 0.53, lod + 3.28).r * 0.2;
	float c = coverage + (w - 0.5) * 2.0 * weather_strength;
	if (radial && latitude_bands > 0.0) {
		float lat = dot(normalize(b), normalize(spin_axis));
		float a = abs(lat);
		float bands = exp(-pow(lat / 0.12, 2.0)) * 0.5 - exp(-pow((a - 0.42) / 0.13, 2.0)) * 0.75
			+ exp(-pow((a - 0.75) / 0.13, 2.0)) * 0.45;
		c += bands * latitude_bands * 0.6;
	}
	return clamp(c, 0.0, 1.0);
}

// Density (extinction per meter) at body-space `b`, height `h` in the layer;
// `pixel`: meters a pixel covers there. `cheap` skips the erosion.
float cloud_density(vec3 b, float h, float pixel, bool cheap) {
	if (h <= 0.0 || h >= 1.0)
		return 0.0;
	float c = weather(b, pixel);
	if (c < 0.01)
		return 0.0;
	// Rounded bases, tops rising higher where it is more overcast.
	float profile = smoothstep(0.0, 0.08, h) * (1.0 - smoothstep(0.25 + 0.7 * c, 1.0, h));
	vec3 moved = b - wind * TIME;
	float lod = lod_for(pixel, cloud_size);
	float n = textureLod(cloud_noise, moved / cloud_size, lod).r;
	float shape = clamp((n * profile - (1.0 - c)) / max(c, 0.05), 0.0, 1.0);
	// Unresolved (single clouds finer than a pixel): a pixel sees a fraction
	// of it covered, not a thin fog over all of it. A thin fog of the same
	// average density would be opaque through the whole layer; this one is as
	// opaque as the covered fraction (optical depth -ln(1 - fraction) through
	// the layer, spread like the clouds' height profile).
	float unresolved = smoothstep(2.0, 5.0, lod);
	if (unresolved > 0.0) {
		float fraction = smoothstep(0.22, 0.7, c);
		float column = -log(1.0 - 0.96 * fraction);
		float mean_profile = 0.55 + 0.35 * c;
		float fog = column / ((cloud_top - cloud_bottom) * mean_profile) * profile;
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
			float e = erosion * mix(1.0 - fbm, fbm, smoothstep(0.1, 0.6, h)) * (1.0 - smoothstep(3.0, 5.0, detail_lod));
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

	// Up to two pieces of the ray inside the layer.
	vec2 seg_a = vec2(1.0, -1.0);
	vec2 seg_b = vec2(1.0, -1.0);
	if (radial) {
		vec2 top = sphere(o, dir, cloud_top);
		if (top.y <= 0.0)
			discard;
		vec2 bottom = sphere(o, dir, cloud_bottom);
		vec2 ground = sphere(o, dir, ground_level);
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
	}
	seg_a.y = min(seg_a.y, seg_a.x + max_distance);
	seg_b.y = min(seg_b.y, seg_b.x + max_distance);
	float len_a = max(seg_a.y - seg_a.x, 0.0);
	float len_b = max(seg_b.y - seg_b.x, 0.0);
	if (len_a + len_b <= 0.0)
		discard;

	vec3 sun = normalize(sun_direction);
	float cos_theta = dot(dir, sun);
	// Forward scattering with a little back scattering; octaves of multiple
	// scattering spread the light out (flatter phase, less extinction).
	float phase0 = mix(hg(forward, cos_theta), hg(-0.3, cos_theta), 0.25) * 4.0 * PI;
	float phase1 = mix(hg(forward * 0.5, cos_theta), hg(-0.15, cos_theta), 0.25) * 4.0 * PI;
	float phase2 = 1.0;
	float jitter = fract(52.9829189 * fract(dot(FRAGCOORD.xy, vec2(0.06711056, 0.00583715))));

	vec3 color = vec3(0.0);
	float transmittance = 1.0;
	float total = len_a + len_b;
	int count = steps;
	// From far away a pixel covers whole clouds: fewer, longer steps do.
	float near_pixel = max(seg_a.x, 1.0) * pixel_angle;
	if (near_pixel > cloud_size)
		count = max(steps / 3, 12);
	// Steps short near the camera, longer further away (quadratic spacing):
	// the near clouds are the ones seen in detail.
	for (int i = 0; i < count; i++) {
		float u = (float(i) + jitter) / float(count);
		float x = u * u * total;
		float dt = 2.0 * u * total / float(count) + total / float(count * count);
		float t = x < len_a ? seg_a.x + x : seg_b.x + (x - len_a);
		vec3 p = o + dir * t;
		float h = layer_height(p);
		float pixel = t * pixel_angle;
		float d = cloud_density(world_to_body * p, h, pixel, false);
		if (d <= 0.0)
			continue;
		vec3 up = radial ? normalize(p) : up_axis;
		// Sun above this point's horizon (the planet's shadow).
		float r = radial ? length(p) : 1.0;
		float horizon = radial ? -sqrt(max(1.0 - pow(ground_level / r, 2.0), 0.0)) : -0.05;
		float daylight = smoothstep(horizon - 0.02, horizon + 0.06, dot(up, sun));
		vec3 direct = vec3(0.0);
		if (daylight > 0.0) {
			float od = optical_depth_to_sun(p, sun, pixel);
			// Octaves of 1, 1/2 and 1/4: normalized (/ 1.75) so a cloud's lit
			// edge sends back about what it receives, as real clouds do.
			float scatter = (exp(-od) * phase0 + 0.5 * exp(-od * 0.25) * phase1 + 0.25 * exp(-od * 0.0625) * phase2) / 1.75;
			direct = sun_color * daylight * scatter;
		}
		float day = smoothstep(-0.25, 0.3, dot(up, sun));
		vec3 ambient = (sky_light * sun_color * sky_strength * day + ambient_light) * mix(0.45, 1.0, clamp(h, 0.0, 1.0));
		vec3 moon = moon_color * smoothstep(-0.1, 0.2, dot(up, normalize(moon_direction))) * mix(0.3, 1.0, h) * 0.6;
		vec3 light = direct + ambient + moon;
		float step_transmittance = exp(-d * dt);
		color += transmittance * light * (1.0 - step_transmittance);
		transmittance *= step_transmittance;
		if (transmittance < 0.01)
			break;
	}
	// Premultiplied: the light the clouds send, and how much they hide.
	float fade = radial ? 1.0 : 1.0 - smoothstep(max_distance * 0.6, max_distance, seg_a.x);
	float alpha = (1.0 - transmittance) * fade;
	if (alpha < 0.003)
		discard;
	ALBEDO = color * cloud_color * brightness * fade;
	ALPHA = alpha;
}
