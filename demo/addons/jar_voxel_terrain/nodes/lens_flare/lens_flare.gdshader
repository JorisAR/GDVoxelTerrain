shader_type spatial;
// Lens flare of a JarLensFlare: drawn over the whole screen after everything
// else, added on top. The sun's position on screen and its light (already
// reddened by an atmosphere) are set every frame; how much of the sun is
// visible is measured here, from the depth buffer around it, so terrain and
// moons hide it (partly while they cover part of the sun). The look is set on
// the node's properties, which fill in the uniforms below.
render_mode unshaded, blend_add, depth_test_disabled, depth_draw_never, cull_disabled, shadows_disabled, fog_disabled;

const int MAX_GHOSTS = 8;

// Set by JarLensFlare every frame.
uniform vec2 sun_uv = vec2(0.5);    // on screen (0..1)
uniform vec3 sun_light = vec3(0.0); // color times energy times intensity
uniform float aspect = 1.7778;      // width / height
uniform float sun_radius = 0.005;   // apparent radius, in screen heights

uniform float glow_strength = 0.35;
uniform float glow_size = 0.12;
uniform float starburst_strength = 0.2;
uniform float starburst_size = 0.2;
uniform float starburst_sharpness = 24.0;
uniform float starburst_rotation = 0.0;
uniform int starburst_rays = 6;
uniform float streak_strength = 0.05;
uniform float streak_width = 0.0045;
uniform float streak_length = 0.33;
uniform vec3 streak_tint = vec3(1.0);
uniform float halo_strength = 0.02;
uniform float halo_radius = 0.42;
uniform float halo_width = 0.03;
uniform float halo_chromatic = 0.04;
uniform float halo_arc = 0.7;
uniform float ghost_strength = 0.03;
uniform float ghost_softness = 0.3;
uniform float ghost_fill = 0.25;
uniform float ghost_chromatic = 0.03;
uniform int ghost_count = 0;
uniform float ghost_positions[MAX_GHOSTS];
uniform float ghost_sizes[MAX_GHOSTS];
uniform vec3 ghost_tints[MAX_GHOSTS];
// Area around the sun sampled for occlusion, in sun radii.
uniform float occlusion_size = 2.0;

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

// A lens ghost at distance d of a radius r: an image of the aperture, faint
// inside and brightest towards its soft rim.
float ghost_shape(float d, float r) {
	float x = d / r;
	if (x >= 1.0)
		return 0.0;
	float edge = 1.0 - smoothstep(1.0 - max(ghost_softness, 0.01), 1.0, x);
	return edge * mix(x * x * x * x, 1.0, ghost_fill);
}

void fragment() {
	// The flare's strengths are of its own: a very bright sun makes a brighter
	// flare, but ever more slowly, or everything would turn solid.
	float peak = max(sun_light.r, max(sun_light.g, sun_light.b));
	if (peak <= 1e-4)
		discard;
	vec3 light = sun_light / (1.0 + 0.5 * peak);
	float visible = visibility(INV_PROJECTION_MATRIX);
	// Fades out as the sun leaves the screen.
	vec2 edge = min(sun_uv, 1.0 - sun_uv);
	visible *= smoothstep(-0.05, 0.08, min(edge.x, edge.y));
	if (visible <= 1e-3)
		discard;

	vec2 to_pixel = (SCREEN_UV - sun_uv) * vec2(aspect, 1.0); // screen heights
	float d = length(to_pixel);
	vec2 axis = (vec2(0.5) - sun_uv) * vec2(aspect, 1.0); // from the sun through the center
	float off_center = length(axis);
	vec3 color = vec3(0.0);

	// Glow: a tight core in a wide, faint veil.
	color += light * glow_strength * (0.7 * exp(-d / max(glow_size, 1e-4) * 6.0) + 0.3 * exp(-d / max(glow_size, 1e-4) * 1.5));

	// Starburst.
	float angle = atan(to_pixel.y, to_pixel.x) + starburst_rotation;
	float rays = pow(abs(cos(angle * float(starburst_rays) * 0.5)), starburst_sharpness);
	color += light * starburst_strength * rays * exp(-d / max(starburst_size, 1e-4) * 4.0);

	// A thin horizontal streak.
	color += light * streak_tint * streak_strength * exp(-abs(to_pixel.y) / max(streak_width, 1e-4)) *
			exp(-abs(to_pixel.x) / max(streak_length, 1e-4));

	// Halo: a ring split into colors, strongest on the side facing the center.
	vec3 ring_radius = halo_radius * vec3(1.0 + halo_chromatic, 1.0, 1.0 - halo_chromatic);
	vec3 ring = (d - ring_radius) / max(halo_width, 1e-4);
	float facing = off_center > 1e-4 && d > 1e-4 ? dot(to_pixel / d, axis / off_center) * 0.5 + 0.5 : 1.0;
	color += light * halo_strength * exp(-ring * ring) * mix(1.0, facing * facing, halo_arc);

	// Ghosts: reflections inside the lens, along the line from the sun through
	// the center, each color at a slightly different size. They gather on the
	// center when the sun is there, so they fade in as it moves away.
	float ghosts = ghost_strength * smoothstep(0.02, 0.3, off_center);
	if (ghosts > 0.0) {
		for (int i = 0; i < min(ghost_count, MAX_GHOSTS); i++) {
			float g = length(to_pixel - axis * ghost_positions[i]);
			float r = ghost_sizes[i];
			vec3 shape = vec3(ghost_shape(g, r * (1.0 + ghost_chromatic)), ghost_shape(g, r), ghost_shape(g, r * (1.0 - ghost_chromatic)));
			color += light * ghost_tints[i] * ghosts * shape;
		}
	}
	ALBEDO = color * visible;
}
