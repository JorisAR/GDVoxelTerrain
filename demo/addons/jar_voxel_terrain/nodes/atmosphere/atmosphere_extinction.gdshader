shader_type spatial;
// First pass of a JarBodyAtmosphere: dims what is drawn behind the air by its
// transmittance, per color, so distant terrain fades towards blue (and the far
// side of a sunset towards red). The sky background is left alone: the sun's
// disk is already reddened through its light's color, and stars do not show
// through a lit sky anyway. atmosphere.gdshader then adds the scattered light.
render_mode unshaded, cull_front, depth_test_disabled, depth_draw_never, blend_mul, shadows_disabled, fog_disabled;

#include "res://addons/jar_voxel_terrain/nodes/atmosphere/atmosphere_common.gdshaderinc"

group_uniforms quality;
uniform int extinction_steps : hint_range(2, 32) = 8;
group_uniforms;

void fragment() {
	vec3 camera = CAMERA_POSITION_WORLD;
	vec3 dir = normalize((INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz - camera);
	bool sky;
	vec2 segment = view_segment(camera, dir, SCREEN_UV, INV_PROJECTION_MATRIX, sky);
	if (sky || segment.y <= segment.x)
		discard;
	float dt = (segment.y - segment.x) / float(extinction_steps);
	vec3 depth = vec3(0.0);
	for (int i = 0; i < extinction_steps; i++) {
		vec3 p = camera + dir * (segment.x + (float(i) + 0.5) * dt);
		depth += medium_at(length(p - planet_center)).extinction * dt;
	}
	ALBEDO = exp(-depth);
}
