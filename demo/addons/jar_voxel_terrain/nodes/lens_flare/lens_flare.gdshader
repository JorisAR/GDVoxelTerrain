shader_type spatial;
// Lens flare of a JarLensFlare: drawn over the whole screen after everything
// else, added on top. The sun's position on screen and its light (already
// reddened by an atmosphere) are set every frame; how much of the sun is
// visible is measured here, from the depth buffer around it, so terrain and
// moons hide it (partly while they cover part of the sun).
render_mode unshaded, blend_add, depth_test_disabled, depth_draw_never, cull_disabled, shadows_disabled, fog_disabled;

// Set by JarLensFlare.
uniform vec2 sun_uv = vec2(0.5);        // on screen (0..1)
uniform vec3 sun_light = vec3(0.0);     // color times energy
uniform float aspect = 1.7778;          // width / height
uniform float sun_radius = 0.005;       // apparent radius, in screen heights

group_uniforms flare;
uniform float intensity = 1.0;
uniform float glow = 0.6;
uniform float glow_size = 0.12;
uniform float starburst = 0.25;
uniform int starburst_rays = 6;
uniform float halo = 0.025;
uniform float halo_radius = 0.42;
uniform float ghosts = 0.12;
uniform float streak = 0.08;
group_uniforms occlusion;
// Area around the sun sampled for occlusion, in sun radii.
uniform float occlusion_size = 2.0;
group_uniforms;

uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;

void vertex() {
	POSITION = vec4(VERTEX.xy, 1.0, 1.0); // the quad covers the screen
}

bool is_sky(vec2 uv, mat4 inv_projection) {
	if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0)
		return true; // off screen: treat as open (the edge fade handles it)
	float ndc = texture(depth_texture, uv).r;
	if (ndc <= 0.0)
		return true;
	vec4 view = inv_projection * vec4(uv * 2.0 - 1.0, ndc, 1.0);
	vec4 far_point = inv_projection * vec4(uv * 2.0 - 1.0, 1e-7, 1.0);
	return length(view.xyz / view.w) > 0.99 * length(far_point.xyz / far_point.w);
}

// Visible part of the sun's disk: open sky in a small grid around it.
float visibility(mat4 inv_projection) {
	float open = 0.0;
	float radius = max(sun_radius, 0.002) * occlusion_size;
	for (int y = -2; y <= 2; y++)
		for (int x = -2; x <= 2; x++) {
			vec2 offset = vec2(float(x) / aspect, float(y)) * radius * 0.5;
			open += is_sky(sun_uv + offset, inv_projection) ? 1.0 : 0.0;
		}
	return open / 25.0;
}

// A soft disk of radius r at distance d (screen heights).
float disk(float d, float r) {
	return 1.0 - smoothstep(r * 0.6, r, d);
}

void fragment() {
	vec3 light = sun_light * intensity;
	if (dot(light, vec3(1.0)) <= 1e-4)
		discard;
	float visible = visibility(INV_PROJECTION_MATRIX);
	// Fades out as the sun leaves the screen.
	vec2 edge = min(sun_uv, 1.0 - sun_uv);
	visible *= smoothstep(-0.05, 0.08, min(edge.x, edge.y));
	if (visible <= 1e-3)
		discard;

	vec2 to_pixel = (SCREEN_UV - sun_uv) * vec2(aspect, 1.0); // screen heights
	float d = length(to_pixel);
	vec3 color = vec3(0.0);
	// Glow and starburst around the sun.
	color += light * glow * exp(-d / glow_size * 4.0);
	float angle = atan(to_pixel.y, to_pixel.x);
	float rays = pow(abs(cos(angle * float(starburst_rays) * 0.5)), 24.0);
	color += light * starburst * rays * exp(-d / glow_size * 2.5);
	// A thin horizontal streak.
	color += light * streak * exp(-abs(to_pixel.y) * 220.0) * exp(-abs(to_pixel.x) * 3.0);
	// A faint halo ring, with a rainbow edge.
	float ring = exp(-pow((d - halo_radius) * 30.0, 2.0));
	color += light * halo * ring * vec3(0.8 + 0.4 * smoothstep(halo_radius - 0.02, halo_radius + 0.02, d), 0.9, 1.1 - 0.3 * smoothstep(halo_radius - 0.02, halo_radius + 0.02, d));
	// Ghosts: reflections inside the lens, along the line from the sun
	// through the screen's center, each slightly tinted.
	vec2 axis = (vec2(0.5) - sun_uv) * vec2(aspect, 1.0);
	const int GHOSTS = 5;
	float positions[GHOSTS] = float[](0.45, 0.8, 1.25, 1.6, 2.2);
	float sizes[GHOSTS] = float[](0.03, 0.06, 0.02, 0.09, 0.05);
	vec3 tints[GHOSTS] = vec3[](vec3(1.0, 0.7, 0.4), vec3(0.5, 0.8, 1.0), vec3(1.0, 1.0, 0.8), vec3(0.6, 1.0, 0.7), vec3(0.8, 0.6, 1.0));
	for (int i = 0; i < GHOSTS; i++) {
		vec2 center = axis * positions[i];
		float g = disk(length(to_pixel - center), sizes[i]);
		color += light * ghosts * g * tints[i];
	}
	ALBEDO = color * visible;
}
