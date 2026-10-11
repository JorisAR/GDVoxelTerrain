shader_type spatial;
// Physically based atmosphere for JarAtmosphere: single scattering by air
// (Rayleigh: blue sky, red sunsets) and haze (Mie: glow around the sun, aerial
// perspective), integrated along each view ray up to the scene depth. The same
// physics applies to the sky and to the ground (no separate tweaks): what keeps
// a small planet from drowning in haze is the density profile, set relative to
// the atmosphere's height.
//
// Light reaching each sample is attenuated with Schüler's analytic
// approximation of the Chapman function (optical depth of an exponential
// atmosphere towards the sun), so the cost is one loop of `view_steps`.
//
// Drawn on the inside of a sphere around the planet as a transparent object
// after everything else (render priority), so it covers terrain, water and
// other transparent objects alike, from the ground and from space. Several
// atmospheres (planets) blend over each other.
render_mode unshaded, cull_front, depth_test_disabled, depth_draw_never, blend_premul_alpha, shadows_disabled, fog_disabled;

// Set by JarAtmosphere (world space).
uniform vec3 planet_center = vec3(0.0);
uniform float planet_radius = 1000.0;
uniform float atmosphere_radius = 1250.0;
uniform vec3 sun_direction = vec3(0.0, 1.0, 0.0); // towards the sun
uniform vec3 sun_color = vec3(1.0);

group_uniforms air;
// Vertical optical depth of the air per color channel (how much light it
// scatters from the ground to space). Earth's is about (0.05, 0.11, 0.27).
uniform vec3 rayleigh_depth = vec3(0.02, 0.045, 0.11);
// Air density falls to 1/e over this fraction of the atmosphere height. Earth
// has about 0.1; a game planet is so small that everything in view is far in
// atmospheric terms, so a gentler profile (less air near the ground for the
// same total) keeps nearby terrain clear while space views keep the glow.
uniform float rayleigh_height : hint_range(0.01, 1.0) = 0.4;
// Haze: vertical optical depth, height fraction, and how strongly it scatters
// forward (glow around the sun).
uniform float mie_depth = 0.012;
uniform float mie_height : hint_range(0.01, 1.0) = 0.15;
uniform float mie_anisotropy : hint_range(0.0, 0.99) = 0.8;
uniform vec3 mie_color : source_color = vec3(1.0);
group_uniforms light;
// Sunlight entering the atmosphere (HDR). Pair with auto exposure.
uniform float sun_intensity = 16.0;
group_uniforms quality;
uniform int view_steps : hint_range(4, 64) = 12;
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

// Chapman function approximation (Schüler, "An Approximation to the Chapman
// Grazing-Incidence Function for Atmospheric Scattering", GPU Pro 3): relative
// optical depth from altitude h (in scale heights) towards a direction with
// zenith cosine cos_z, on a planet of radius X scale heights.
float chapman(float X, float h, float cos_z) {
	float c = sqrt(X + h);
	if (cos_z >= 0.0)
		return c / (c * cos_z + 1.0) * exp(-h);
	float x0 = sqrt(1.0 - cos_z * cos_z) * (X + h);
	float c0 = sqrt(x0);
	return 2.0 * c0 * exp(X - x0) - c / (1.0 - c * cos_z) * exp(-h);
}

void fragment() {
	vec3 camera = CAMERA_POSITION_WORLD;
	vec3 frag = (INV_VIEW_MATRIX * vec4(VERTEX, 1.0)).xyz;
	vec3 dir = normalize(frag - camera);

	// Distance to whatever the camera sees (terrain, water floor) along the ray.
	float ndc = texture(depth_texture, SCREEN_UV).r;
	vec4 view = INV_PROJECTION_MATRIX * vec4(SCREEN_UV * 2.0 - 1.0, ndc, 1.0);
	float scene = length(view.xyz / view.w);
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
	float h_r = rayleigh_height * thickness;
	float h_m = mie_height * thickness;
	// Scattering coefficients per meter at the surface: optical depth / scale height.
	vec3 beta_r = rayleigh_depth / h_r;
	float beta_m = mie_depth / h_m;
	vec3 sun = normalize(sun_direction);

	float mu = dot(dir, sun);
	float phase_r = 3.0 / (16.0 * PI) * (1.0 + mu * mu);
	float g = mie_anisotropy;
	float phase_m = 3.0 / (8.0 * PI) * ((1.0 - g * g) * (1.0 + mu * mu)) / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * mu, 1.5));

	float step_length = (end - start) / float(view_steps);
	// Per-pixel jitter of the sample positions: turns banding into fine noise.
	float jitter = fract(52.9829189 * fract(dot(FRAGCOORD.xy, vec2(0.06711056, 0.00583715))));
	vec2 view_depth = vec2(0.0); // optical depth (Rayleigh, Mie units) camera -> sample
	vec3 sum_r = vec3(0.0);
	vec3 sum_m = vec3(0.0);
	for (int i = 0; i < view_steps; i++) {
		vec3 p = camera + dir * (start + (float(i) + jitter) * step_length);
		vec3 up = p - planet_center;
		float r = length(up);
		up /= r;
		float altitude = max(r - planet_radius, 0.0);
		vec2 d = vec2(exp(-altitude / h_r), exp(-altitude / h_m)) * step_length;
		view_depth += d;
		// Light from the sun: none in the planet's shadow.
		float cos_sun = dot(up, sun);
		vec2 shadow = sphere_hits(p, sun, planet_radius);
		if (shadow.y > 0.0 && shadow.x > 0.0)
			continue;
		vec2 light_depth = vec2(h_r * chapman(planet_radius / h_r, altitude / h_r, cos_sun),
								h_m * chapman(planet_radius / h_m, altitude / h_m, cos_sun));
		vec3 tau = beta_r * (view_depth.x + light_depth.x) + beta_m * 1.1 * (view_depth.y + light_depth.y);
		vec3 attenuation = exp(-tau);
		sum_r += d.x * attenuation;
		sum_m += d.y * attenuation;
	}
	vec3 inscatter = sun_intensity * sun_color * (sum_r * beta_r * phase_r + sum_m * beta_m * mie_color * phase_m);
	vec3 transmittance = exp(-(beta_r * view_depth.x + beta_m * 1.1 * view_depth.y));

	ALBEDO = inscatter;
	// Premultiplied: what is behind is dimmed by the transmittance.
	ALPHA = 1.0 - dot(transmittance, vec3(1.0 / 3.0));
}
