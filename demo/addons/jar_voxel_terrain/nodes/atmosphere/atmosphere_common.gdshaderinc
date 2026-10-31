// Shared by the atmosphere's two passes (atmosphere.gdshader: light scattered
// towards the camera, added; atmosphere_extinction.gdshader: what is behind,
// dimmed per color). Everything here is set by JarBodyAtmosphere from its
// properties (world space, meters) and must match its CPU model
// (atmosphere_model.cpp): a smooth sphere with Rayleigh and Mie scattering and
// ozone absorption, after Hillaire 2020.

uniform vec3 planet_center = vec3(0.0);
uniform float planet_radius = 1000.0;     // sea level
uniform float atmosphere_radius = 1250.0; // top
uniform vec3 sun_direction = vec3(0.0, 1.0, 0.0); // towards the sun
uniform vec3 sun_color = vec3(1.0);       // sunlight above the atmosphere

uniform vec3 rayleigh_scattering = vec3(0.0); // per meter at sea level
uniform float rayleigh_scale_height = 100.0;  // meters
uniform float mie_scattering = 0.0;
uniform float mie_scale_height = 40.0;
uniform float mie_absorption = 0.11;
uniform float mie_anisotropy = 0.7;
uniform vec3 ozone_absorption = vec3(0.0);    // per meter at the layer's peak
uniform float ozone_center = 90.0;            // meters above sea level
uniform float ozone_half_width = 40.0;

uniform sampler2D transmittance_lut : filter_linear, repeat_disable;
uniform sampler2D multiscattering_lut : filter_linear, repeat_disable;

uniform sampler2D depth_texture : hint_depth_texture, filter_nearest;

const int TRANSMITTANCE_WIDTH = 256;
const int TRANSMITTANCE_HEIGHT = 64;
const int MULTISCATTERING_SIZE = 32;

// Distances along the ray to the sphere around the planet (near, far); far < 0: no hit.
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

struct Medium {
	vec3 rayleigh;   // scattering
	float mie;       // scattering
	vec3 extinction;
};

Medium medium_at(float r) {
	float h = max(r - planet_radius, 0.0);
	float ozone = max(0.0, 1.0 - abs(h - ozone_center) / ozone_half_width);
	// Everything fades to nothing over the top tenth (as the CPU model).
	float thickness = atmosphere_radius - planet_radius;
	float fade = smoothstep(0.0, 1.0, clamp((thickness - h) / (0.1 * thickness), 0.0, 1.0));
	Medium m;
	m.rayleigh = rayleigh_scattering * exp(-h / rayleigh_scale_height) * fade;
	m.mie = mie_scattering * exp(-h / mie_scale_height) * fade;
	m.extinction = m.rayleigh + vec3(m.mie * (1.0 + mie_absorption)) + ozone_absorption * ozone * fade;
	return m;
}

float unit_to_texel(float x, int n) {
	return 0.5 / float(n) + x * (1.0 - 1.0 / float(n));
}

// Transmittance to space from radius r along zenith cosine mu (Bruneton's mapping).
vec3 transmittance_to_space(float r, float mu) {
	float bottom2 = planet_radius * planet_radius;
	float top2 = atmosphere_radius * atmosphere_radius;
	float H = sqrt(top2 - bottom2);
	float rho = sqrt(max(r * r - bottom2, 0.0));
	float d = max(0.0, -r * mu + sqrt(max(r * r * (mu * mu - 1.0) + top2, 0.0)));
	float d_min = atmosphere_radius - r;
	float d_max = rho + H;
	vec2 uv = vec2(unit_to_texel((d - d_min) / max(d_max - d_min, 1e-6), TRANSMITTANCE_WIDTH),
				   unit_to_texel(rho / H, TRANSMITTANCE_HEIGHT));
	return texture(transmittance_lut, uv).rgb;
}

// Sunlight left at radius r with the sun at zenith cosine mu_sun (none behind the planet).
vec3 sunlight_at(float r, float mu_sun) {
	r = clamp(r, planet_radius, atmosphere_radius);
	float sin_horizon = planet_radius / r;
	float mu_horizon = -sqrt(max(0.0, 1.0 - sin_horizon * sin_horizon));
	return transmittance_to_space(r, mu_sun) * smoothstep(mu_horizon - 0.01, mu_horizon + 0.01, mu_sun);
}

vec3 multiple_scattering_at(float r, float mu_sun) {
	vec2 uv = vec2(unit_to_texel(clamp(mu_sun * 0.5 + 0.5, 0.0, 1.0), MULTISCATTERING_SIZE),
				   unit_to_texel(clamp((r - planet_radius) / (atmosphere_radius - planet_radius), 0.0, 1.0), MULTISCATTERING_SIZE));
	return texture(multiscattering_lut, uv).rgb;
}

float rayleigh_phase(float nu) {
	return 3.0 / (16.0 * PI) * (1.0 + nu * nu);
}

float mie_phase(float nu, float g) {
	float g2 = g * g;
	return 3.0 / (8.0 * PI) * ((1.0 - g2) * (1.0 + nu * nu)) / ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * g * nu, 1e-6), 1.5));
}

// The part of the view ray inside the atmosphere and in front of what is
// drawn there (x: start, y: end; y <= x: nothing). `sky` when nothing is drawn.
vec2 view_segment(vec3 camera, vec3 dir, vec2 screen_uv, mat4 inv_projection, out bool sky) {
	float ndc = texture(depth_texture, screen_uv).r;
	vec4 view = inv_projection * vec4(screen_uv * 2.0 - 1.0, ndc, 1.0);
	float scene = length(view.xyz / view.w);
	vec4 far_point = inv_projection * vec4(screen_uv * 2.0 - 1.0, 1e-7, 1.0);
	sky = ndc <= 0.0 || scene > 0.99 * length(far_point.xyz / far_point.w);
	vec2 air = sphere_hits(camera, dir, atmosphere_radius);
	if (air.y < 0.0)
		return vec2(0.0);
	float start = max(air.x, 0.0);
	float end = air.y;
	if (!sky)
		end = min(end, scene);
	// The planet itself stops the ray where the terrain is not drawn (far away).
	vec2 ground = sphere_hits(camera, dir, planet_radius);
	if (ground.y > 0.0 && ground.x > 0.0)
		end = min(end, ground.x);
	return vec2(start, end);
}
