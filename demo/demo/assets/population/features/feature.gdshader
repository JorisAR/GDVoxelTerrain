shader_type spatial;
// Low-poly features (trees, bushes, rocks): vertex colored, with normals bent
// towards the instance's up axis. Foliage then shades softly like a canopy
// lit from above instead of showing every facet, and objects match the light
// on the ground around them. Trees sway gently (by height above their base).

uniform float roughness : hint_range(0.0, 1.0) = 0.9;
// 0 keeps the mesh normals, 1 lights everything like flat ground.
uniform float up_normals : hint_range(0.0, 1.0) = 0.6;
uniform float wind_strength = 0.0;
uniform float wind_speed = 0.8;

void vertex() {
	if (wind_strength > 0.0) {
		vec3 origin = MODEL_MATRIX[3].xyz;
		float phase = TIME * wind_speed + dot(origin, vec3(0.05, 0.03, 0.04));
		float bend = max(VERTEX.y, 0.0) * wind_strength * 0.02;
		VERTEX.xz += vec2(sin(phase), cos(phase * 0.8)) * bend * bend;
	}
	NORMAL = normalize(mix(NORMAL, vec3(0.0, 1.0, 0.0), up_normals));
}

void fragment() {
	// Vertex colors are authored in sRGB.
	vec3 c = COLOR.rgb;
	ALBEDO = mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
	ROUGHNESS = roughness;
	SPECULAR = 0.2;
}
