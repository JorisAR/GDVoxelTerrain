shader_type spatial;
// Light the atmosphere of a JarBodyAtmosphere scatters towards the camera:
// the sky, the glow seen from space, and the haze in front of distant terrain
// (aerial perspective). Physically based (Hillaire 2020): Rayleigh scattering
// (blue sky, red and orange sunsets), Mie scattering (haze, glow around the
// sun), ozone absorption (deep blue twilight) and multiple scattering
// (precomputed, so twilight and the edge of the night side are not black).
// Sunlight through the air comes from a precomputed transmittance table, so
// each pixel marches its view ray once.
//
// Drawn on a sphere around the planet after everything else (render
// priority), added on top. Before it, atmosphere_extinction.gdshader dims
// what is behind per color (distant mountains turn blue). Several atmospheres
// (planets) add up. The atmosphere's physics are properties of
// JarBodyAtmosphere (they also set the sunlight's color and the ambient
// light); this material only has the look.
render_mode unshaded, cull_front, depth_test_disabled, depth_draw_never, blend_add, shadows_disabled, fog_disabled;

#include "res://addons/jar_voxel_terrain/nodes/atmosphere/atmosphere_common.gdshaderinc"

group_uniforms light;
// Sunlight entering the atmosphere (HDR). Pair with auto exposure.
uniform float sun_intensity = 16.0;
group_uniforms quality;
uniform int view_steps : hint_range(4, 64) = 16;
group_uniforms;

void fragment() {
	vec3 camera = CAMERA_POSITION_WORLD;
	vec3 dir = normalize((INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz - camera);
	bool sky;
	vec2 segment = view_segment(camera, dir, SCREEN_UV, INV_PROJECTION_MATRIX, sky);
	if (segment.y <= segment.x)
		discard;

	vec3 sun = normalize(sun_direction);
	float nu = dot(dir, sun);
	float phase_r = rayleigh_phase(nu);
	float phase_m = mie_phase(nu, mie_anisotropy);

	// Samples spaced more densely near the camera (where the haze matters).
	float length_ = segment.y - segment.x;
	float jitter = fract(52.9829189 * fract(dot(FRAGCOORD.xy, vec2(0.06711056, 0.00583715))));
	vec3 radiance = vec3(0.0);
	vec3 throughput = vec3(1.0);
	float previous = 0.0;
	for (int i = 0; i < view_steps; i++) {
		float x = (float(i) + 1.0) / float(view_steps);
		float t = x * x * length_;
		float dt = t - previous;
		float t_sample = previous + dt * jitter;
		previous = t;
		vec3 p = camera + dir * (segment.x + t_sample);
		vec3 up = p - planet_center;
		float r = length(up);
		float mu_sun = dot(up / r, sun);
		Medium m = medium_at(r);
		vec3 sunlight = sunlight_at(r, mu_sun);
		vec3 scattering = m.rayleigh + vec3(m.mie);
		vec3 source = m.rayleigh * phase_r * sunlight + vec3(m.mie * phase_m) * sunlight + scattering * multiple_scattering_at(r, mu_sun);
		vec3 step_transmittance = exp(-m.extinction * dt);
		radiance += throughput * (source - source * step_transmittance) / max(m.extinction, vec3(1e-9));
		throughput *= step_transmittance;
	}
	ALBEDO = radiance * sun_intensity * sun_color;
}
