shader_type spatial;
// Planetary rings for JarBodyRings: a flat annulus whose density and color
// along the radius come from the ring profile. Ring particles are lit like
// a dusty, icy layer:
// - from the lit side, Lommel-Seeliger (a layer of scatterers, not a solid
//   surface), a little brighter looking away from the sun (opposition);
// - from the unlit side, only light that got through: thin rings glow, dense
//   ones are dark;
// - towards the sun, fine dust scatters forward: the rings light up;
// - edge-on they turn opaque (longer path), and the planet's shadow falls on them.
render_mode unshaded, cull_disabled, depth_draw_never, blend_mix, shadows_disabled, fog_disabled;

#include "res://addons/jar_voxel_terrain/nodes/rings/rings_common.gdshaderinc"

group_uniforms look;
uniform float brightness = 1.4;
// Strength of the glow with the sun behind the rings (fine dust).
uniform float forward_scatter : hint_range(0.0, 4.0) = 1.2;
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

void fragment() {
	vec4 ring = jar_ring_at(world_position);
	if (ring.a <= 0.002)
		discard;
	vec3 to_eye = normalize(CAMERA_POSITION_WORLD - world_position);
	vec3 sun = normalize(sun_direction);
	float view_side = dot(to_eye, ring_normal);
	float sun_side = dot(sun, ring_normal);
	float mu = abs(view_side);
	float mu0 = abs(sun_side);
	float alpha = jar_ring_opacity(ring.a, mu);

	// Lommel-Seeliger, normalized so face-on light is 1.
	float ls = 2.0 * mu0 / (mu0 + mu + 1e-4);
	float lit;
	if (view_side * sun_side >= 0.0) {
		float cos_phase = dot(sun, to_eye); // 1: the sun behind the viewer
		lit = ls * (1.0 + opposition * pow(max(cos_phase, 0.0), 8.0));
	} else {
		// Through the rings: what a layer this thick lets through and scatters.
		lit = ls * (1.0 - ring.a) * 1.5;
	}
	// Forward scattering by dust: strongest where the rings are thin.
	float forward = henyey_greenstein(0.75, dot(-sun, to_eye)) * 4.0 * PI;
	lit += forward_scatter * forward * 0.15 * (1.0 - ring.a) / max(mu, 0.15);
	lit *= jar_planet_transmittance(world_position);

	ALBEDO = ring.rgb * (sun_color * lit * brightness + ambient_light);
	ALPHA = alpha;
}
