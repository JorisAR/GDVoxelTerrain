// Shared by rings.gdshader and gas_giant.gdshader. Set by JarBodyRings /
// JarBodyGasGiant (world space; scaled space when the body is far away).
uniform vec3 planet_center = vec3(0.0);
uniform float planet_radius = 1.0;
uniform vec3 ring_normal = vec3(0.0, 1.0, 0.0);
uniform float ring_inner = 1.24;
uniform float ring_outer = 2.33;
// Color in rgb, opacity seen face-on in alpha, from the inner to the outer edge.
uniform sampler2D ring_profile : source_color, filter_linear_mipmap, repeat_disable;
uniform vec3 sun_direction = vec3(0.0, 1.0, 0.0); // towards the star
uniform vec3 sun_color = vec3(1.0);

// The ring profile where a point lies in the ring plane (alpha 0 off the rings).
vec4 jar_ring_at(vec3 p) {
	float x = (length(p - planet_center) - ring_inner) / (ring_outer - ring_inner);
	if (x < 0.0 || x > 1.0)
		return vec4(0.0);
	return texture(ring_profile, vec2(x, 0.5));
}

// Opacity of a ring of face-on opacity `a` along a direction at cosine `mu`
// to the ring plane's normal: the path through it is 1 / mu times as long.
float jar_ring_opacity(float a, float mu) {
	return 1.0 - pow(max(1.0 - a, 0.0), 1.0 / max(mu, 0.01));
}

// How much sunlight the rings let through to a point (1: none in the way).
float jar_ring_transmittance(vec3 p) {
	float facing = dot(sun_direction, ring_normal);
	if (abs(facing) < 1e-4)
		return 1.0;
	float t = -dot(p - planet_center, ring_normal) / facing;
	if (t <= 0.0)
		return 1.0;
	vec4 ring = jar_ring_at(p + sun_direction * t);
	return 1.0 - jar_ring_opacity(ring.a, abs(facing));
}

// How much sunlight the planet lets through to a point: its shadow, with a
// soft edge (the star's size and the air at the limb).
float jar_planet_transmittance(vec3 p) {
	vec3 d = p - planet_center;
	float along = dot(d, sun_direction);
	if (along > 0.0)
		return 1.0; // on the day side of the planet
	float off_axis = length(d - sun_direction * along);
	return smoothstep(planet_radius * 0.985, planet_radius * 1.015, off_axis);
}
