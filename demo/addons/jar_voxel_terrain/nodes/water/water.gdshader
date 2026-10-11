shader_type spatial;
// Water surface for JarPlanetWater. Vertices are projected onto the exact sea
// level (a sphere for planets, a plane for flat worlds), so a coarse mesh still
// meets the coast precisely. Color comes from the water depth behind each
// pixel (absorption), with procedural waves, shore foam and fresnel.
render_mode blend_mix, depth_draw_always, cull_back, world_vertex_coords;

// Set by JarPlanetWater (world space).
uniform vec3 water_center = vec3(0.0);
uniform vec3 water_axis = vec3(0.0, 1.0, 0.0);
uniform float water_level = 0.0; // radius (radial) or height along the axis
uniform bool water_radial = true;

group_uniforms color;
uniform vec4 shallow_color : source_color = vec4(0.10, 0.45, 0.50, 1.0);
uniform vec4 deep_color : source_color = vec4(0.02, 0.10, 0.22, 1.0);
// Meters of water after which the bottom is no longer visible.
uniform float clarity = 12.0;
// Meters of water over which the color goes from shallow to deep.
uniform float depth_range = 30.0;
uniform float roughness : hint_range(0.0, 1.0) = 0.06;

group_uniforms waves;
uniform float wave_size = 6.0;
uniform float wave_strength : hint_range(0.0, 1.0) = 0.35;
uniform float wave_speed = 0.6;

group_uniforms foam;
uniform vec4 foam_color : source_color = vec4(0.9, 0.95, 1.0, 1.0);
uniform float foam_width = 1.2;
group_uniforms;

uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;

// The coarse distant mesh skips what the dense patch around the camera covers
// (both are transparent: drawing both would double the water).
instance uniform vec3 hole_center = vec3(0.0);
instance uniform float hole_radius = 0.0;

varying vec3 world_position;
varying vec3 surface_up;

void vertex() {
	vec3 p = VERTEX;
	if (water_radial) {
		vec3 d = p - water_center;
		surface_up = normalize(d);
		p = water_center + surface_up * water_level;
	} else {
		surface_up = water_axis;
		p += water_axis * (water_level - dot(p - water_center, water_axis));
	}
	VERTEX = p;
	NORMAL = surface_up;
	world_position = p;
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

// Wave height: two octaves drifting in different directions.
float waves(vec3 p, float t) {
	return noise(p / wave_size + vec3(t, 0.0, t * 0.6)) * 0.65 + noise(p / (wave_size * 0.43) - vec3(t * 0.8, t * 0.3, 0.0)) * 0.35;
}

void fragment() {
	if (hole_radius > 0.0 && distance(world_position, hole_center) < hole_radius)
		discard;
	vec3 up = normalize(surface_up);
	float t = TIME * wave_speed / wave_size;
	// Normal from the wave height gradient along the surface.
	vec3 t1 = normalize(cross(up, abs(up.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0)));
	vec3 t2 = cross(up, t1);
	float e = wave_size * 0.05;
	float h = waves(world_position, t);
	float hx = waves(world_position + t1 * e, t);
	float hz = waves(world_position + t2 * e, t);
	float view_distance = length(VERTEX);
	float strength = wave_strength * (1.0 - smoothstep(wave_size * 20.0, wave_size * 120.0, view_distance));
	vec3 n = normalize(up - (t1 * (hx - h) + t2 * (hz - h)) / e * strength * wave_size * 0.2);
	NORMAL = normalize((VIEW_MATRIX * vec4(n, 0.0)).xyz);

	// Water depth behind this pixel along the view ray.
	float scene_ndc = texture(depth_texture, SCREEN_UV).r;
	vec4 scene_view = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, scene_ndc, 1.0);
	float scene_distance = length(scene_view.xyz / scene_view.w);
	float water = max(scene_distance - view_distance, 0.0);

	float deep = 1.0 - exp(-water / depth_range);
	vec3 color = mix(shallow_color.rgb, deep_color.rgb, deep);
	float opacity = 1.0 - exp(-water / clarity);
	float foam = (1.0 - smoothstep(0.0, foam_width, water)) * smoothstep(0.35, 0.65, noise(world_position * 0.7 + vec3(TIME * 0.3)) + 0.3);
	foam *= 1.0 - smoothstep(foam_width * 60.0, foam_width * 200.0, view_distance); // only a line from afar

	ALBEDO = mix(color, foam_color.rgb, foam);
	ALPHA = clamp(max(opacity, foam) * 0.92 + 0.08, 0.0, 1.0);
	ROUGHNESS = mix(roughness, 0.8, foam);
	SPECULAR = 0.5;
}
