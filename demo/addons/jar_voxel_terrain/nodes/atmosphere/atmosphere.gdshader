shader_type spatial;
// Physically based atmosphere for JarAtmosphere: single scattering by air
// (Rayleigh: blue sky, red sunsets) and haze (Mie: glow around the sun, aerial
// perspective), integrated along each view ray up to the scene depth.
//
// Drawn on the inside of a sphere around the planet as a transparent object
// after everything else (render priority), so it covers terrain, water and
// other transparent objects alike, from the ground and from space. Several
// atmospheres (planets) blend over each other.
render_mode unshaded, cull_front, depth_test_disabled, depth_draw_never, blend_premul_alpha, shadows_disabled, fog_disabled;

// Set by JarAtmosphere (world space).
uniform vec3 planet_center = vec3(0.0);
uniform float planet_radius = 1000.0;
uniform float atmosphere_radius = 1120.0;
uniform vec3 sun_direction = vec3(0.0, 1.0, 0.0); // towards the sun

group_uniforms air;
// Vertical optical depth of the air per color channel (how much light it
// scatters from top to bottom). Earth's is about (0.05, 0.11, 0.27).
uniform vec3 rayleigh_depth = vec3(0.05, 0.11, 0.27);
// Fraction of the atmosphere height where the air density falls to 1/e.
uniform float rayleigh_height : hint_range(0.01, 1.0) = 0.2;
// Vertical optical depth of haze, its height fraction and how strongly it
// scatters forward (glow around the sun).
uniform float mie_depth = 0.02;
uniform float mie_height : hint_range(0.01, 1.0) = 0.05;
uniform float mie_anisotropy : hint_range(0.0, 0.99) = 0.76;
uniform vec3 mie_color : source_color = vec3(1.0);
group_uniforms light;
uniform float sun_intensity = 12.0;
// Scales the haze over terrain (aerial perspective) against the sky. Game
// planets are tiny: everything in view is many atmosphere heights away, so the
// physical amount (1) washes the ground out. The sky always uses 1.
uniform float aerial_perspective : hint_range(0.0, 1.0) = 0.04;
// How strongly a bright sky hides what is behind it (stars by day).
uniform float sky_occlusion : hint_range(0.0, 8.0) = 4.0;
group_uniforms quality;
uniform int view_steps : hint_range(4, 64) = 16;
uniform int light_steps : hint_range(2, 16) = 4;
group_uniforms;

uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;


// Distances along the ray to the sphere (near, far); far < 0 means no hit.
vec2 sphere_hits(vec3 origin, vec3 dir, float radius) {
	vec3 oc = origin - planet_center;
	float b = dot(oc, dir);
	float c = dot(oc, oc) - radius * radius;
	float h = b * b - c;
	if (h < 0.0)
		return vec2(1e30, -1.0);
	h = sqrt(h);
	return vec2(-b - h, -b + h);
}

// Air and haze density (relative to sea level) at a point.
vec2 density(vec3 p, float thickness) {
	float height = max(length(p - planet_center) - planet_radius, 0.0) / thickness;
	return vec2(exp(-height / rayleigh_height), exp(-height / mie_height));
}

void fragment() {
	vec3 camera = CAMERA_POSITION_WORLD;
	vec3 frag = (INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec3 dir = normalize(frag - camera);

	// Distance to whatever the camera sees (terrain, water floor) along the ray.
	float ndc = texture(depth_texture, SCREEN_UV).r;
	vec4 view = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, ndc, 1.0);
	float scene = length(view.xyz / view.w);
	// The far plane is "nothing": the sky.
	vec4 far_point = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, 1e-7, 1.0);
	bool sky = ndc <= 0.0 || scene > 0.99 * length(far_point.xyz / far_point.w);

	vec2 air = sphere_hits(camera, dir, atmosphere_radius);
	if (air.y < 0.0)
		discard;
	float start = max(air.x, 0.0);
	float end = air.y;
	if (!sky)
		end = min(end, scene);
	// The planet itself stops the ray where the terrain is not drawn (far away).
	vec2 ground = sphere_hits(camera, dir, planet_radius);
	if (ground.y > 0.0 && ground.x > 0.0)
		end = min(end, ground.x);
	if (end <= start)
		discard;

	float thickness = atmosphere_radius - planet_radius;
	// Scattering coefficients per meter at sea level: optical depth / scale height.
	vec3 beta_r = rayleigh_depth / (rayleigh_height * thickness);
	float beta_m = mie_depth / (mie_height * thickness);

	float mu = dot(dir, normalize(sun_direction));
	float phase_r = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
	float g = mie_anisotropy;
	float phase_m = 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu)) / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * mu, 1.5));

	float step_length = (end - start) / float(view_steps);
	vec2 view_depth = vec2(0.0);
	vec3 sum_r = vec3(0.0);
	vec3 sum_m = vec3(0.0);
	for (int i = 0; i < view_steps; i++) {
		vec3 p = camera + dir * (start + (float(i) + 0.5) * step_length);
		vec2 d = density(p, thickness) * step_length;
		view_depth += d;
		// Light reaching p from the sun (zero in the planet's shadow).
		vec2 sun_hit = sphere_hits(p, sun_direction, atmosphere_radius);
		vec2 shadow = sphere_hits(p, sun_direction, planet_radius);
		if (shadow.y > 0.0 && shadow.x > 0.0)
			continue;
		float light_length = sun_hit.y / float(light_steps);
		vec2 light_depth = vec2(0.0);
		for (int j = 0; j < light_steps; j++)
			light_depth += density(p + sun_direction * (float(j) + 0.5) * light_length, thickness) * light_length;
		vec3 tau = beta_r * (view_depth.x + light_depth.x) + beta_m * 1.1 * (view_depth.y + light_depth.y);
		vec3 attenuation = exp(-tau);
		sum_r += d.x * attenuation;
		sum_m += d.y * attenuation;
	}
	float strength = sky ? 1.0 : aerial_perspective;
	vec3 inscatter = sun_intensity * (sum_r * beta_r * phase_r + sum_m * beta_m * mie_color * phase_m) * strength;
	vec3 transmittance = exp(-(beta_r * view_depth.x + beta_m * 1.1 * view_depth.y) * strength);

	ALBEDO = inscatter;
	// Premultiplied: what is behind is dimmed by the transmittance.
	float alpha = 1.0 - dot(transmittance, vec3(1.0 / 3.0));
	if (sky) // a bright day sky outshines the stars behind it
		alpha = max(alpha, clamp(dot(inscatter, vec3(0.3, 0.5, 0.2)) * sky_occlusion, 0.0, 1.0));
	ALPHA = alpha;
}
