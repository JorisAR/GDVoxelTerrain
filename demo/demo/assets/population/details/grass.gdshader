shader_type spatial;
// Grass blades lit like the ground they grow on: the normal is the instance's
// up axis (the terrain normal, with align_with_normal), not the blade's own
// face, so a meadow shades as one surface instead of thousands of flat cards.
// Blades sway in the wind and shrink into the ground before the draw distance
// instead of popping. Vertex color red: 0 at the root, 1 at the tip.
render_mode cull_disabled;

uniform vec3 base_color : source_color = vec3(0.10, 0.17, 0.06);
uniform vec3 tip_color : source_color = vec3(0.36, 0.48, 0.17);
// Random per-instance color variation.
uniform float variation : hint_range(0.0, 1.0) = 0.25;
uniform float wind_strength = 0.08;
uniform float wind_speed = 1.4;
// Grass shrinks between these distances (meters).
uniform float fade_start = 40.0;
uniform float fade_end = 60.0;

varying float tip;
varying vec3 tint;

void vertex() {
	tip = COLOR.r;
	vec3 origin = MODEL_MATRIX[3].xyz;
	float random = fract(sin(dot(origin, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
	tint = mix(vec3(1.0), vec3(1.15, 1.05, 0.8), random * variation);
	// Shrink with distance to the camera.
	float distance_to_camera = length(origin - INV_VIEW_MATRIX[3].xyz);
	float shrink = 1.0 - smoothstep(fade_start, fade_end, distance_to_camera);
	VERTEX.y *= shrink;
	// Wind: sway the tip, in the blade's local frame.
	float phase = TIME * wind_speed + dot(origin, vec3(0.13, 0.07, 0.11));
	VERTEX.xz += vec2(sin(phase), cos(phase * 0.7)) * wind_strength * tip * shrink;
	// Light it like the ground: the instance's up axis.
	NORMAL = vec3(0.0, 1.0, 0.0);
}

void fragment() {
	ALBEDO = mix(base_color, tip_color, tip) * tint;
	// Blades let some light through; a soft rim keeps their backs from going black.
	BACKLIGHT = vec3(0.2) * tip;
	ROUGHNESS = 0.85;
	SPECULAR = 0.15;
}
