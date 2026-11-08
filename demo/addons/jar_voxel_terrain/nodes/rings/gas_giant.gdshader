shader_type spatial;
// A gas giant for JarBodyGasGiant: cloud bands along latitude in three
// colors, broken up by turbulence that moves with the zonal winds, with
// streaks, a few oval storms, darker poles and limb darkening; lit by the
// star, with the shadow of its rings (JarBodyRings on the same body).
// Everything is noise in this shader, nothing is baked.
render_mode unshaded, cull_disabled, shadows_disabled, fog_disabled;

#include "res://addons/jar_voxel_terrain/nodes/rings/rings_common.gdshaderinc"

uniform mat3 world_to_body = mat3(1.0);
uniform vec3 spin_axis = vec3(0.0, 1.0, 0.0); // in body space
uniform vec3 spin_axis_world = vec3(0.0, 1.0, 0.0);
uniform float flattening = 0.0; // the shell is squashed along the spin axis
uniform bool has_rings = false;

group_uniforms bands;
uniform vec3 color_light : source_color = vec3(0.80, 0.72, 0.55);
uniform vec3 color_dark : source_color = vec3(0.62, 0.49, 0.34);
uniform vec3 color_accent : source_color = vec3(0.72, 0.55, 0.38);
uniform vec3 color_polar : source_color = vec3(0.50, 0.55, 0.58);
// Number of bands from pole to pole.
uniform float band_count = 14.0;
uniform float band_contrast : hint_range(0.0, 1.0) = 0.55;
// How much the bands are torn up.
uniform float turbulence : hint_range(0.0, 2.0) = 0.6;
// Where the polar color takes over (sine of the latitude).
uniform float polar_latitude : hint_range(0.0, 1.0) = 0.82;
uniform float storms : hint_range(0.0, 1.0) = 0.4;
uniform int seed = 3;
// Turns per hour of the fastest wind relative to the body (bands drift).
uniform float wind_speed = 0.02;
// One great long-lived storm (like Jupiter's red spot): its latitude (sine),
// longitude (radians), size (radians across) and strength (0: none).
uniform vec4 great_spot = vec4(-0.38, 1.0, 0.16, 0.0);
uniform vec3 great_spot_color : source_color = vec3(0.75, 0.38, 0.24);
group_uniforms light;
uniform float brightness = 1.0;
// Minnaert exponent: 0.5 flat disk, 1 Lambert. Gas giants are dark at the limb.
uniform float limb_darkening : hint_range(0.5, 1.5) = 0.85;
uniform vec3 ambient_light = vec3(0.004, 0.004, 0.005);
group_uniforms;

varying vec3 world_position;

void vertex() {
	world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

float hash(vec3 p) {
	p = fract(p * 0.3183099 + 0.1 + float(seed) * 0.0137);
	p *= 17.0;
	return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float noise(vec3 x) {
	vec3 i = floor(x);
	vec3 f = fract(x);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(hash(i), hash(i + vec3(1, 0, 0)), f.x), mix(hash(i + vec3(0, 1, 0)), hash(i + vec3(1, 1, 0)), f.x), f.y),
			   mix(mix(hash(i + vec3(0, 0, 1)), hash(i + vec3(1, 0, 1)), f.x), mix(hash(i + vec3(0, 1, 1)), hash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

// Octaves finer than a pixel (`pixel`, in units of p) fade to their mean.
float fbm(vec3 p, float pixel) {
	float v = 0.0;
	float a = 0.5;
	for (int i = 0; i < 6; i++) {
		v += a * mix(noise(p), 0.5, smoothstep(0.25, 0.6, pixel));
		p = p * 2.07 + 7.3;
		a *= 0.5;
		pixel *= 2.07;
	}
	return v;
}

void fragment() {
	// Inside (flown into): there is no surface, only deep, dim cloud.
	if (!FRONT_FACING) {
		ALBEDO = mix(color_dark, color_light, 0.5) * (sun_color * 0.02 + ambient_light);
	} else {
		vec3 d = world_position - planet_center;
		vec3 polar = normalize(spin_axis_world);
		// The ellipsoid's normal: the polar part of the offset counts more.
		float k = 1.0 / ((1.0 - flattening) * (1.0 - flattening));
		vec3 normal = normalize(d + polar * dot(d, polar) * (k - 1.0));
		// On the unit sphere (unsquashed), turning with the body.
		vec3 local = normalize(world_to_body * (d + polar * dot(d, polar) * (1.0 / (1.0 - flattening) - 1.0)));
		vec3 axis = normalize(spin_axis);
		float lat = dot(local, axis); // sine of the latitude
		float pixel = max(length(dFdx(local)), length(dFdy(local)));

		// Zonal winds: alternating jets turn the cloud deck at different speeds.
		vec3 east = normalize(cross(axis, abs(axis.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
		vec3 north = cross(east, axis);
		float lon = atan(dot(local, north), dot(local, east));
		float jet = sin(lat * band_count * 1.5708);
		float drift = TIME / 3600.0 * wind_speed * 6.2832 * jet;
		float r = sqrt(max(1.0 - lat * lat, 0.0));
		vec3 flow = axis * lat + (east * cos(lon + drift) + north * sin(lon + drift)) * r;

		// Bands: latitude, displaced by turbulence stretched along the winds.
		vec3 stretched = flow * 3.0;
		float warp = (fbm(stretched + vec3(0.0, lat * 12.0, 0.0), pixel * 3.0) - 0.5) * turbulence * 0.25;
		float band_lat = lat + warp / band_count * 4.0;
		float bands = sin(band_lat * band_count * 1.5708 * 2.0 + (fbm(vec3(band_lat * 6.0, 0.0, 1.7), 0.0) - 0.5) * 4.0);
		float accent = smoothstep(0.55, 1.0, fbm(vec3(band_lat * 20.0, 3.1, 0.0), 0.0));
		vec3 color = mix(color_light, color_dark, (bands * 0.5 + 0.5) * band_contrast);
		color = mix(color, color_accent, accent * 0.5);
		// Streaks: fine detail along the winds.
		float streak = fbm(vec3(flow.x * 4.0, lat * 160.0, flow.z * 4.0), pixel * 80.0);
		color *= 0.9 + 0.2 * streak;
		// Oval storms, light or dark, in a few bands.
		vec3 storm_cell = floor(flow * 5.0);
		float storm_pick = hash(storm_cell + 0.5);
		if (storm_pick < storms * 0.25) {
			vec3 center = (storm_cell + 0.5 + (vec3(hash(storm_cell + 1.3), hash(storm_cell + 2.7), hash(storm_cell + 4.1)) - 0.5) * 0.5) / 5.0;
			vec3 offset = flow - center;
			// Ovals: twice as long east-west as north-south.
			float north_south = dot(offset, axis);
			float east_west = length(offset - axis * north_south);
			float d = length(vec2(east_west, north_south * 2.2)) * 5.0;
			float storm = 1.0 - smoothstep(0.15, 0.35, d);
			color = mix(color, storm_pick < storms * 0.12 ? color_light * 1.05 : color_accent * 0.8, storm * 0.8);
		}
		// The great storm: an oval swirl with a pale collar, drifting with its band.
		if (great_spot.w > 0.0) {
			float spot_drift = TIME / 3600.0 * wind_speed * 6.2832 * sin(great_spot.x * band_count * 1.5708);
			float slon = great_spot.y + spot_drift;
			float sr = sqrt(max(1.0 - great_spot.x * great_spot.x, 0.0));
			vec3 spot_center = axis * great_spot.x + (east * cos(slon) + north * sin(slon)) * sr;
			vec3 off = local - spot_center;
			float ns = dot(off, axis);
			vec3 ew_dir = off - axis * ns;
			float ew = length(ew_dir);
			float sd = length(vec2(ew, ns * 1.7)) / (great_spot.z * 0.5);
			// Swirl: the inside turns, so its streaks curve around the center.
			float angle = atan(ns * 1.7, dot(ew_dir, east * -sin(slon) + north * cos(slon))) + sd * 3.0;
			float swirl = fbm(vec3(cos(angle) * sd * 3.0, sin(angle) * sd * 3.0, 4.2), pixel / max(great_spot.z, 1e-3) * 6.0);
			float spot = 1.0 - smoothstep(0.55, 1.0, sd);
			float collar = smoothstep(0.75, 1.0, sd) * (1.0 - smoothstep(1.0, 1.35, sd));
			color = mix(color, great_spot_color * (0.8 + 0.4 * swirl), spot * great_spot.w);
			color = mix(color, color_light, collar * 0.6 * great_spot.w);
		}
		// Poles: bluer, smoother.
		color = mix(color, color_polar, smoothstep(polar_latitude, polar_latitude + 0.12, abs(lat)));

		// Light: Minnaert limb darkening, the rings' shadow, a soft terminator.
		vec3 to_eye = normalize(CAMERA_POSITION_WORLD - world_position);
		vec3 sun = normalize(sun_direction);
		float mu0 = dot(normal, sun);
		float mu = max(dot(normal, to_eye), 0.0);
		float direct = pow(max(mu0, 0.0), limb_darkening) * pow(max(mu, 1e-3), limb_darkening - 1.0);
		direct *= smoothstep(-0.02, 0.08, mu0);
		if (has_rings)
			direct *= jar_ring_transmittance(world_position);
		// The terminator reddens a little (light through the upper haze).
		vec3 tint = mix(vec3(1.0, 0.75, 0.55), vec3(1.0), smoothstep(0.0, 0.25, mu0));
		ALBEDO = color * (sun_color * tint * direct * brightness + ambient_light);
	}
}
