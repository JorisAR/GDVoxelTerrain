shader_type spatial;
// Clouds for JarBodyClouds. The clouds themselves are traced by
// JarCloudsEffect (a compositor effect, clouds_trace.glsl and
// clouds_resolve.glsl) at half resolution and accumulated over frames; this
// shader only puts them on screen, at full resolution, after the water, the
// rings and the atmosphere, with the atmosphere's haze between the camera and
// the clouds (the atmosphere itself hazes what is behind them by the
// distance to the terrain: a cloud in front of far mountains would be hazed
// like the mountains). It is drawn on the back faces of a shell around the
// layer (so it covers the screen from inside and the planet from outside).
//
// Upsampling: of the four half resolution pixels around a pixel, those whose
// ray ended at a nearer surface than this pixel's (an edge of terrain in
// front) count little. Their rays end at the nearest surface of their block,
// so no cloud behind a surface is ever drawn over it.
//
// The uniforms below the first group are the clouds' look: JarBodyClouds
// reads them from this material every frame.
render_mode unshaded, cull_front, depth_test_disabled, depth_draw_never, blend_premul_alpha, shadows_disabled, fog_disabled;

// The body's air (planet_center, radii, sun, scattering, tables, depth_texture):
// JarBodyClouds copies them from the body's JarBodyAtmosphere.
#include "res://addons/jar_voxel_terrain/nodes/atmosphere/atmosphere_common.gdshaderinc"

// Set by JarBodyClouds.
uniform sampler2D cloud_color_buffer : filter_nearest; // premultiplied color, opacity
uniform sampler2D cloud_depth_buffer : filter_nearest; // distance of the clouds seen, where the ray ended
uniform bool cloud_ready = false;
uniform bool has_atmosphere = false;
uniform float sun_intensity = 16.0; // the atmosphere material's

group_uniforms shape;
// Shifts the weather map's coverage (0.5: as it is; 0 clear, 1 overcast).
uniform float coverage : hint_range(0.0, 1.0) = 0.5;
// How much of the weather map's contrast is kept (0: the same coverage everywhere).
uniform float weather_strength : hint_range(0.0, 1.0) = 1.0;
// Flat worlds (no weather map): size of weather systems in meters.
uniform float weather_size = 1500.0;
// Regions inside the weather (clusters, gaps, streets stretched east-west):
// their size in meters (0: 6 times cloud_size) and how much they change the
// coverage.
uniform float region_size = 0.0;
uniform float region_strength : hint_range(0.0, 1.0) = 0.5;
// The period of the cloud noise in meters (4 single clouds across it).
uniform float cloud_size = 1600.0;
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
// Darker edges on the side facing away from the sun.
uniform float powder : hint_range(0.0, 1.0) = 0.5;
group_uniforms quality;
// Most steps along a ray (fewer where a pixel covers more; frames add up).
uniform int steps : hint_range(8, 160) = 48;
uniform int light_steps : hint_range(1, 8) = 4;
// Rays stop this far (meters); flat worlds also fade out towards it.
uniform float max_distance = 60000.0;
// How much of each new frame goes into the result (less: smoother, slower to follow).
uniform float history_blend : hint_range(0.02, 1.0) = 0.12;
// Steps of the haze between the camera and the clouds.
uniform int haze_steps : hint_range(2, 16) = 8;
group_uniforms;

const float FAR = 1e30;

// The air's light and transmittance between the camera and `distance` along
// `dir` (atmosphere.gdshader's march, shorter).
void haze(vec3 camera, vec3 dir, float distance, out vec3 light, out vec3 transmittance) {
	light = vec3(0.0);
	transmittance = vec3(1.0);
	vec2 air = sphere_hits(camera, dir, atmosphere_radius);
	float start = max(air.x, 0.0);
	float end = min(air.y, distance);
	if (air.y < 0.0 || end <= start)
		return;
	vec3 sun = normalize(sun_direction);
	float nu = dot(dir, sun);
	float phase_r = rayleigh_phase(nu);
	float phase_m = mie_phase(nu, mie_anisotropy);
	float length_ = end - start;
	float previous = 0.0;
	for (int i = 0; i < haze_steps; i++) {
		float x = (float(i) + 1.0) / float(haze_steps);
		float t = x * x * length_;
		float dt = t - previous;
		vec3 q = camera + dir * (start + previous + dt * 0.5);
		previous = t;
		vec3 up = q - planet_center;
		float r = length(up);
		float mu_sun = dot(up / r, sun);
		Medium m = medium_at(r);
		vec3 sunlight = sunlight_at(r, mu_sun);
		vec3 scattering = m.rayleigh + vec3(m.mie);
		vec3 source = m.rayleigh * phase_r * sunlight + vec3(m.mie * phase_m) * sunlight + scattering * multiple_scattering_at(r, mu_sun);
		vec3 step_transmittance = exp(-m.extinction * dt);
		light += transmittance * (source - source * step_transmittance) / max(m.extinction, vec3(1e-9));
		transmittance *= step_transmittance;
	}
	light *= sun_intensity * sun_color;
}

void fragment() {
	if (!cloud_ready)
		discard;
	ivec2 full = ivec2(VIEWPORT_SIZE);
	ivec2 low = (full + 1) / 2;
	ivec2 fp = ivec2(FRAGCOORD.xy);
	float ndc = texelFetch(depth_texture, fp, 0).r;
	vec4 view = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, ndc, 1.0);
	float scene = length(view.xyz / view.w);
	vec4 far_point = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, 1e-7, 1.0);
	if (ndc <= 0.0 || scene > 0.99 * length(far_point.xyz / far_point.w))
		scene = FAR;

	vec2 q = (vec2(fp) + 0.5) * 0.5 - 0.5;
	ivec2 base = ivec2(floor(q));
	vec2 f = q - vec2(base);
	vec4 sum = vec4(0.0);
	float front = 0.0;
	float weights = 0.0;
	for (int i = 0; i < 4; i++) {
		ivec2 o = ivec2(i & 1, i >> 1);
		ivec2 t = clamp(base + o, ivec2(0), low - 1);
		float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y) + 1e-4;
		vec2 d = texelFetch(cloud_depth_buffer, t, 0).xy;
		// Its ray ended at a nearer surface that is not in front here.
		if (d.y < scene * 0.9)
			w *= 0.02;
		vec4 c = texelFetch(cloud_color_buffer, t, 0);
		sum += w * c;
		front += w * c.a * min(d.x, 1e9);
		weights += w;
	}
	vec4 c = sum / weights;
	if (c.a < 0.003)
		discard;
	if (has_atmosphere) {
		// Hazed as far as the clouds are (the air behind them is hidden).
		vec3 camera = CAMERA_POSITION_WORLD;
		vec3 dir = normalize((INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz - camera);
		vec3 light, transmittance;
		haze(camera, dir, front / max(sum.a, 1e-5), light, transmittance);
		c.rgb = c.rgb * transmittance + light * c.a;
	}
	ALBEDO = c.rgb;
	ALPHA = c.a;
}
