shader_type spatial;
// Underwater fog for JarPlanetWater. Drawn on a small sphere around the camera
// (inside faces, no depth test, so it covers the screen) and only does anything
// when the camera is below the sea level: everything is fogged by the length of
// its path through the water (up to the scene depth, or to the surface when
// looking up), and light fades with depth below the surface.
render_mode unshaded, cull_front, depth_test_disabled, depth_draw_never, blend_premul_alpha, shadows_disabled, fog_disabled;

// Set by JarPlanetWater (world space).
uniform vec3 water_center = vec3(0.0);
uniform vec3 water_axis = vec3(0.0, 1.0, 0.0);
uniform float water_level = 0.0;
uniform bool water_radial = true;

uniform vec3 fog_color : source_color = vec3(0.03, 0.18, 0.24);
// Meters after which things disappear into the fog.
uniform float visibility = 25.0;
// Meters below the surface over which the light fades to a dim blue.
uniform float light_depth = 40.0;

uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;

void fragment() {
	vec3 camera = CAMERA_POSITION_WORLD;
	float below = water_radial ? water_level - length(camera - water_center)
							   : water_level - dot(camera - water_center, water_axis);
	if (below <= 0.0)
		discard;
	vec3 frag = (INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec3 dir = normalize(frag - camera);

	float ndc = texture(depth_texture, SCREEN_UV).r;
	vec4 view = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, ndc, 1.0);
	float scene = ndc <= 0.0 ? 1e30 : length(view.xyz / view.w);

	// Where the ray leaves the water.
	float exit = 1e30;
	if (water_radial) {
		vec3 oc = camera - water_center;
		float b = dot(oc, dir);
		float c = dot(oc, oc) - water_level * water_level;
		exit = -b + sqrt(max(b * b - c, 0.0));
	} else {
		float rise = dot(dir, water_axis);
		if (rise > 1e-4)
			exit = below / rise;
	}
	float path = min(scene, exit);
	float fog = 1.0 - exp(-path / visibility);
	float light = mix(0.25, 1.0, exp(-below / light_depth));
	ALBEDO = fog_color * light * fog;
	ALPHA = fog;
}
