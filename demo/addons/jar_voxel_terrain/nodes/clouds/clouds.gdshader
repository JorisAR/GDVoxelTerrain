shader_type spatial;
// Clouds for JarBodyClouds. The clouds themselves are traced by
// JarCloudsEffect (a compositor effect, clouds_trace.glsl and
// clouds_resolve.glsl) at half resolution and accumulated over frames; this
// shader only puts them on screen, at full resolution, in their place among
// the other layers (after water and rings, before the atmosphere that hazes
// them). It is drawn on the back faces of a shell around the layer (so it
// covers the screen from inside and the planet from outside).
//
// Upsampling: of the four half resolution pixels around a pixel, those whose
// ray ended at a nearer surface than this pixel's (an edge of terrain in
// front) count little. Their rays end at the nearest surface of their block,
// so no cloud behind a surface is ever drawn over it.
//
// The uniforms below the first group are the clouds' look: JarBodyClouds
// reads them from this material every frame.
render_mode unshaded, cull_front, depth_test_disabled, depth_draw_never, blend_premul_alpha, shadows_disabled, fog_disabled;

// Set by JarBodyClouds.
uniform sampler2D cloud_color_buffer : filter_nearest; // premultiplied color, opacity
uniform sampler2D cloud_depth_buffer : filter_nearest; // distance of the clouds seen, where the ray ended
uniform bool cloud_ready = false;
uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;

group_uniforms shape;
// Shifts the weather map's coverage (0.5: as it is; 0 clear, 1 overcast).
uniform float coverage : hint_range(0.0, 1.0) = 0.5;
// How much of the weather map's contrast is kept (0: the same coverage everywhere).
uniform float weather_strength : hint_range(0.0, 1.0) = 1.0;
// Flat worlds (no weather map): size of weather systems in meters.
uniform float weather_size = 1500.0;
// Regions inside the weather (clusters, gaps, streets stretched east-west):
// their size in meters (0: 6 times cloud_size) and how much they change the
// coverage.
uniform float region_size = 0.0;
uniform float region_strength : hint_range(0.0, 1.0) = 0.5;
// The period of the cloud noise in meters (4 single clouds across it).
uniform float cloud_size = 220.0;
// Size of the eroded detail, relative to cloud_size, and how much it eats.
uniform float detail_scale : hint_range(0.02, 1.0) = 0.18;
uniform float erosion : hint_range(0.0, 1.0) = 0.4;
// How dense clouds are (extinction per meter inside a full cloud).
uniform float density = 0.03;
uniform vec3 wind = vec3(6.0, 0.0, 2.0); // meters per second
// The weather drifts around the planet: radians per hour about its axis.
uniform float weather_drift = 0.02;
group_uniforms light;
uniform vec3 cloud_color : source_color = vec3(1.0);
uniform float brightness = 1.0;
// Light from the blue sky (times the sunlight), from above.
uniform vec3 sky_light : source_color = vec3(0.38, 0.5, 0.75);
uniform float sky_strength : hint_range(0.0, 1.0) = 0.25;
// Forward scattering: bright edges with the sun behind the clouds.
uniform float forward : hint_range(0.0, 0.95) = 0.6;
// Darker edges on the side facing away from the sun.
uniform float powder : hint_range(0.0, 1.0) = 0.5;
group_uniforms quality;
// Most steps along a ray (fewer where a pixel covers more; frames add up).
uniform int steps : hint_range(8, 160) = 48;
uniform int light_steps : hint_range(1, 8) = 4;
// Rays stop this far (meters); flat worlds also fade out towards it.
uniform float max_distance = 60000.0;
// How much of each new frame goes into the result (less: smoother, slower to follow).
uniform float history_blend : hint_range(0.02, 1.0) = 0.12;
group_uniforms;

const float FAR = 1e30;

void fragment() {
	if (!cloud_ready)
		discard;
	ivec2 full = ivec2(VIEWPORT_SIZE);
	ivec2 low = (full + 1) / 2;
	ivec2 fp = ivec2(FRAGCOORD.xy);
	float ndc = texelFetch(depth_texture, fp, 0).r;
	vec4 view = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, ndc, 1.0);
	float scene = length(view.xyz / view.w);
	vec4 far_point = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, 1e-7, 1.0);
	if (ndc <= 0.0 || scene > 0.99 * length(far_point.xyz / far_point.w))
		scene = FAR;

	vec2 q = (vec2(fp) + 0.5) * 0.5 - 0.5;
	ivec2 base = ivec2(floor(q));
	vec2 f = q - vec2(base);
	vec4 sum = vec4(0.0);
	float weights = 0.0;
	for (int i = 0; i < 4; i++) {
		ivec2 o = ivec2(i & 1, i >> 1);
		ivec2 t = clamp(base + o, ivec2(0), low - 1);
		float w = (o.x == 1 ? f.x : 1.0 - f.x) * (o.y == 1 ? f.y : 1.0 - f.y) + 1e-4;
		float end = texelFetch(cloud_depth_buffer, t, 0).y;
		// Its ray ended at a nearer surface that is not in front here.
		if (end < scene * 0.9)
			w *= 0.02;
		sum += w * texelFetch(cloud_color_buffer, t, 0);
		weights += w;
	}
	vec4 c = sum / weights;
	if (c.a < 0.003)
		discard;
	ALBEDO = c.rgb;
	ALPHA = c.a;
}
