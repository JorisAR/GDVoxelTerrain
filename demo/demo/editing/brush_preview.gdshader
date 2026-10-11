shader_type spatial;
// Ghost of the edit brush: a fresnel rim that stays visible through terrain.
render_mode unshaded, cull_disabled, depth_draw_never, depth_test_disabled, shadows_disabled;

uniform vec4 rim_color : source_color = vec4(0.35, 0.85, 1.0, 1.0);
uniform float rim_power = 2.5;
uniform float fill = 0.06;

void fragment() {
	float facing = abs(dot(normalize(NORMAL), normalize(VIEW)));
	float rim = pow(1.0 - facing, rim_power);
	ALBEDO = rim_color.rgb;
	ALPHA = clamp(rim + fill, 0.0, 1.0) * rim_color.a;
}
