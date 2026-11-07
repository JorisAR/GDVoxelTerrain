shader_type spatial;
// Planetary rings for JarBodyRings: a flat annulus whose density and color
// along the radius come from the ring profile. A ring is a thin layer of icy
// particles of optical depth tau (from the profile's opacity); each pixel is
// the light it scatters once towards the eye (single scattering), with the
// phase of big rough particles (like little moons: bright with the sun
// behind the viewer, dark looking towards the sun) plus a little dust that
// scatters forward:
// - from the lit side the rings shine, dense rings most;
// - from the unlit side only what gets through: thin rings glow, dense ones
//   go dark;
// - towards the sun the dust lights up;
// - edge-on they turn opaque (longer path), and the planet's shadow falls on them.
render_mode unshaded, cull_disabled, depth_draw_never, blend_mix, shadows_disabled, fog_disabled;

#include "res://addons/jar_voxel_terrain/nodes/rings/rings_common.gdshaderinc"

group_uniforms look;
uniform float brightness = 1.0;
// Strength of the glow with the sun behind the rings (fine dust; most in
// the thin rings and gaps).
uniform float forward_scatter : hint_range(0.0, 4.0) = 1.0;
// Brightening looking from the sun's side (opposition surge).
uniform float opposition : hint_range(0.0, 2.0) = 0.4;
// Light from the sky and stars on the night side.
uniform vec3 ambient_light = vec3(0.004, 0.004, 0.005);
group_uniforms;

varying vec3 world_position;

void vertex() {
	world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
}

float henyey_greenstein(float g, float cos_theta) {
	float d = 1.0 + g * g - 2.0 * g * cos_theta;
	return (1.0 - g * g) / (4.0 * PI * d * sqrt(d));
}

// Phase function of a Lambert sphere at phase angle `phase` (1 at full).
float lambert_sphere(float phase) {
	return (sin(phase) + (PI - phase) * cos(phase)) / PI;
}

void fragment() {
	vec4 ring = jar_ring_at(world_position);
	if (ring.a <= 0.002)
		discard;
	vec3 to_eye = normalize(CAMERA_POSITION_WORLD - world_position);
	vec3 sun = normalize(sun_direction);
	float mu = max(abs(dot(to_eye, ring_normal)), 0.01);
	float mu0 = max(abs(dot(sun, ring_normal)), 0.01);
	bool lit_side = dot(to_eye, ring_normal) * dot(sun, ring_normal) >= 0.0;
	float tau = -log(max(1.0 - ring.a, 1e-3)); // optical depth seen face-on
	float alpha = 1.0 - exp(-tau / mu);

	float cos_phase = dot(sun, to_eye); // 1: the sun behind the viewer
	float phase = acos(clamp(cos_phase, -1.0, 1.0));
	float particles = lambert_sphere(phase) * (1.0 + opposition * pow(max(cos_phase, 0.0), 8.0));
	// Single scattering by a layer (I/F, times 8 so a dense ring face-on
	// under an overhead sun is 1), for the lit and the unlit side.
	float layer;
	if (lit_side)
		layer = 2.0 * mu0 / (mu0 + mu) * (1.0 - exp(-tau * (1.0 / mu0 + 1.0 / mu)));
	else if (abs(mu - mu0) > 1e-3)
		layer = 2.0 * mu0 / (mu - mu0) * (exp(-tau / mu) - exp(-tau / mu0));
	else
		layer = 2.0 * tau / mu * exp(-tau / mu);
	float lit = layer * particles;
	// Dust: optical depth highest relative to the ice in thin rings; scatters
	// forward (Henyey-Greenstein), seen through the layer in front of it.
	float dust_tau = 0.08 * tau * (1.0 - ring.a) + 0.004;
	float forward = henyey_greenstein(0.75, dot(-sun, to_eye)) * 4.0 * PI;
	lit += forward_scatter * forward * dust_tau / mu * exp(-tau / mu * 0.5);
	lit *= jar_planet_transmittance(world_position);

	// Blended over what is behind with the layer's opacity: the color is the
	// light per unit of opacity.
	ALBEDO = ring.rgb * (sun_color * lit * brightness / max(alpha, 1e-3) + ambient_light);
	ALPHA = alpha;
}
