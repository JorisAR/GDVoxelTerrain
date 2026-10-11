shader_type spatial;
// Basic cloud layer for JarClouds: a shell at a fixed altitude with coverage
// from drifting 3D noise. Lit by the sun with cheap self-shadowing (density
// sampled towards the sun) and dark on the night side. Drawn after water and
// before the atmosphere, which hazes it like everything else.
render_mode unshaded, cull_disabled, depth_draw_never, blend_mix, shadows_disabled, world_vertex_coords;

// Set by JarClouds (world space).
uniform vec3 planet_center = vec3(0.0);
uniform vec3 up_axis = vec3(0.0, 1.0, 0.0);
uniform bool radial = true;
uniform vec3 sun_direction = vec3(0.0, 1.0, 0.0);
uniform vec3 sun_color = vec3(1.0);

group_uniforms shape;
// Fraction of the sky covered, on average.
uniform float coverage : hint_range(0.0, 1.0) = 0.35;
// Weather systems: coverage varies by this much over `weather_size` meters,
// so there are clear and overcast regions instead of even speckles.
uniform float weather_strength : hint_range(0.0, 1.0) = 0.5;
uniform float weather_size = 1500.0;
// Size of the cloud features, in meters.
uniform float cloud_size = 220.0;
// Edge softness (0 crisp, 1 very soft).
uniform float softness : hint_range(0.01, 1.0) = 0.25;
uniform float opacity : hint_range(0.0, 1.0) = 0.9;
uniform vec3 wind = vec3(6.0, 0.0, 2.0); // meters per second
group_uniforms light;
uniform vec3 cloud_color : source_color = vec3(1.0);
uniform vec3 shadow_color : source_color = vec3(0.45, 0.5, 0.6);
uniform float brightness = 1.6;
// Flat worlds: fade out with distance (meters), so the layer's edge is never seen.
uniform float fade_distance = 6000.0;
group_uniforms;

varying vec3 world_position;

void vertex() {
	world_position = VERTEX;
}

float hash(vec3 p) {
	p = fract(p * 0.3183099 + 0.1);
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

float fbm(vec3 p) {
	float v = 0.0;
	float a = 0.5;
	for (int i = 0; i < 5; i++) {
		v += a * noise(p);
		p = p * 2.03 + 11.7;
		a *= 0.5;
	}
	return v;
}

float density(vec3 p) {
	float n = fbm(p / cloud_size);
	float weather = (noise(p / weather_size + 5.3) - 0.5) * 2.0 * weather_strength;
	float threshold = 1.0 - clamp(coverage + weather * 0.5, 0.0, 1.0);
	return smoothstep(threshold - softness * 0.5, threshold + softness * 0.5, n);
}

void fragment() {
	vec3 p = world_position - planet_center - wind * TIME;
	float d = density(p);
	if (d < 0.01)
		discard;
	vec3 up = radial ? normalize(world_position - planet_center) : up_axis;
	vec3 sun = normalize(sun_direction);
	// Self-shadowing: denser cloud towards the sun = darker here.
	float towards_sun = density(p + sun * cloud_size * 0.35);
	float lit = exp(-towards_sun * 2.0) * 0.8 + 0.2;
	// Day and night: sunlight reaches the layer only where the sun is up.
	float day = smoothstep(-0.15, 0.2, dot(up, sun));
	vec3 color = mix(shadow_color, cloud_color, lit) * sun_color * brightness * (0.08 + 0.92 * day);
	// Only a flat layer has an edge to hide; a planet's shell has none.
	float fade = radial ? 1.0 : 1.0 - smoothstep(fade_distance * 0.6, fade_distance, length(VERTEX));
	ALBEDO = color;
	ALPHA = d * opacity * fade;
}
