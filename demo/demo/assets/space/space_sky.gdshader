shader_type sky;
// Space background in physical-ish brightness: a starfield with a realistic
// magnitude distribution (many faint stars, few bright ones) and colors from
// star temperatures, a faint galactic band, and the sun's disk with limb
// darkening and a small corona.
//
// Stars are far dimmer than a sunlit sky. With auto exposure (see the demo
// environment's camera attributes) they vanish behind a bright day sky and
// appear in space or at night, without special casing.

group_uniforms stars;
// Brightness of the brightest stars (HDR).
uniform float star_brightness = 0.08;
// Angular radius of a star's glow, in radians (about a pixel).
uniform float star_size = 0.0009;
// Stars per layer cell; higher = more stars.
uniform float star_density : hint_range(0.0, 1.0) = 0.35;
group_uniforms galaxy;
uniform vec3 galaxy_normal = vec3(0.3, 0.9, 0.3);
uniform vec3 galaxy_color : source_color = vec3(0.55, 0.6, 0.75);
uniform float galaxy_brightness = 0.012;
group_uniforms sun;
// Disk brightness relative to the light energy (HDR; the disk is far brighter
// than anything else).
uniform float sun_disk_intensity = 40.0;
uniform float sun_corona_intensity = 0.4;
// Used when the light has no angular size set.
uniform float sun_default_size_degrees = 0.8;
group_uniforms;

float hash13(vec3 p) {
	p = fract(p * 0.1031);
	p += dot(p, p.zyx + 31.32);
	return fract((p.x + p.y) * p.z);
}

vec3 hash33(vec3 p) {
	p = fract(p * vec3(0.1031, 0.1030, 0.0973));
	p += dot(p, p.yxz + 33.33);
	return fract((p.xxy + p.yxx) * p.zyx);
}

float value_noise(vec3 x) {
	vec3 i = floor(x);
	vec3 f = fract(x);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(mix(hash13(i), hash13(i + vec3(1, 0, 0)), f.x), mix(hash13(i + vec3(0, 1, 0)), hash13(i + vec3(1, 1, 0)), f.x), f.y),
			   mix(mix(hash13(i + vec3(0, 0, 1)), hash13(i + vec3(1, 0, 1)), f.x), mix(hash13(i + vec3(0, 1, 1)), hash13(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

// Approximate color of a black body (star temperature t in 0..1: 3000K..12000K).
vec3 star_color(float t) {
	vec3 red = vec3(1.0, 0.62, 0.38);
	vec3 white = vec3(1.0, 0.96, 0.92);
	vec3 blue = vec3(0.68, 0.78, 1.0);
	return t < 0.5 ? mix(red, white, t * 2.0) : mix(white, blue, (t - 0.5) * 2.0);
}

// One layer of stars: at most one star per cell of a grid around the sphere,
// kept away from the cell's edges so a pixel only needs its own cell.
vec3 star_layer(vec3 dir, float scale, float brightness) {
	vec3 cell = floor(dir * scale);
	vec3 r = hash33(cell);
	if (r.x > star_density)
		return vec3(0.0);
	vec3 star = normalize(cell + 0.15 + 0.7 * hash33(cell + 41.0));
	float angle = acos(clamp(dot(dir, star), -1.0, 1.0));
	// Magnitudes: brightness follows a steep power law.
	float magnitude = pow(hash13(cell + 17.0), 10.0);
	float glow = exp(-angle * angle / (star_size * star_size));
	return star_color(r.y) * magnitude * glow * brightness;
}

void sky() {
	vec3 dir = EYEDIR;
	vec3 color = vec3(0.00015, 0.0002, 0.0004);

	// Galactic band: brighter near the plane, broken up by noise.
	float band = 1.0 - abs(dot(dir, normalize(galaxy_normal)));
	float clouds = value_noise(dir * 6.0) * 0.6 + value_noise(dir * 17.0) * 0.4;
	color += galaxy_color * galaxy_brightness * pow(band, 6.0) * (0.4 + clouds);
	// Three layers: few bright stars, many faint ones.
	color += star_layer(dir, 90.0, star_brightness);
	color += star_layer(dir, 180.0, star_brightness * 0.35);
	color += star_layer(dir, 360.0, star_brightness * 0.12);

	if (LIGHT0_ENABLED) {
		float size = LIGHT0_SIZE > 0.0 ? LIGHT0_SIZE * 0.5 : radians(sun_default_size_degrees) * 0.5;
		float angle = acos(clamp(dot(dir, LIGHT0_DIRECTION), -1.0, 1.0));
		float r = angle / size;
		if (r < 1.0) {
			// Limb darkening: the edge of the disk is dimmer and redder.
			float mu = sqrt(1.0 - r * r);
			vec3 limb = vec3(pow(mu, 0.4), pow(mu, 0.5), pow(mu, 0.65));
			color += LIGHT0_COLOR * LIGHT0_ENERGY * sun_disk_intensity * limb;
		}
		// Corona: a soft glow falling off quickly around the disk.
		color += LIGHT0_COLOR * LIGHT0_ENERGY * sun_corona_intensity * exp(-max(r - 1.0, 0.0) * 1.5) * 0.02;
	}
	COLOR = color;
}
