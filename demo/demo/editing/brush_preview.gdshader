shader_type spatial;
// Ghost of the edit brush. It is depth tested like any object, so it sits in
// the world; where it cuts through geometry a bright band outlines the
// intersection, and a faint fresnel rim shows its shape above the surface.
render_mode unshaded, cull_disabled, depth_draw_never, shadows_disabled;

uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;
uniform vec4 color : source_color = vec4(0.35, 0.85, 1.0, 1.0);
// Width of the intersection outline, in meters (set from the brush size).
uniform float band_width = 0.15;
uniform float rim_power = 3.0;
uniform float rim_strength = 0.35;
uniform float fill = 0.04;

void fragment() {
	// Linear view-space depth of whatever is behind this pixel.
	float scene_ndc = texture(depth_texture, SCREEN_UV).r;
	vec4 scene_view = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, scene_ndc, 1.0);
	float scene_depth = -scene_view.z / scene_view.w;
	float own_depth = -VERTEX.z;

	float outline = 1.0 - smoothstep(0.0, band_width, abs(scene_depth - own_depth));
	float facing = abs(dot(normalize(NORMAL), normalize(VIEW)));
	float rim = pow(1.0 - facing, rim_power) * rim_strength;

	ALBEDO = color.rgb;
	ALPHA = clamp(outline + rim + fill, 0.0, 1.0) * color.a;
}
