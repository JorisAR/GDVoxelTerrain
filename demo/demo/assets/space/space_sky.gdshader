shader_type sky;

// Comment this if you don't want to use star twinke. However, if you do want to keep it,
// I suggest you to set the sky process mode to High-Quality Incremental or Real-Time,
// as star twinkling may greatly impact performance.
#define USE_TWINKLE

group_uniforms sky;
uniform vec3 sky_color: source_color = vec3(0.03, 0.05, 0.11);

group_uniforms stars;
uniform vec3 star_base_color: source_color = vec3(0.8, 1.0, 0.3);
uniform float star_hue_offset: hint_range(0., 1.) = 0.6;
uniform float star_intensity: hint_range(0., 0.2) = 0.08;
#ifdef USE_TWINKLE
uniform float star_twinkle_speed: hint_range(0.0, 2.0) = 0.8;
uniform float star_twinkle_intensity: hint_range(0.0, 1.0) = 0.2;
#endif

group_uniforms layers;
uniform float layer_scale: hint_range(0., 60.) = 20.;
uniform float layer_scale_step: hint_range(0., 40.) = 10.;
uniform int layers_count: hint_range(0, 12) = 3;

// Hue credit:
// The MIT License
// Copyright © 2024 DigvijaysinhGohil
// https://github.com/DigvijaysinhGohil/Godot-Shader-Lib

vec3 hue(vec3 input, float offset, int range_index) {
	// RGB to HSV
	vec4 k = vec4(0., -1./3., 2./3., -1.);
	vec4 p = mix(vec4(input.bg, k.wz), vec4(input.gb, k.xy), step(input.b, input.g));
	vec4 q = mix(vec4(p.xyw, input.r), vec4(input.r, p.yzx), step(p.x, input.r));
	float d = q.x - min(q.w, q.y);
	float e = 1.e-10;
	vec3 hsv = vec3(abs(q.z + (q.w - q.y) / (6. * d + e)), d / (q.x + e), q.x);

	offset = (range_index == 0) ? offset / 360. : offset;
	float hue = hsv.x + offset;
	if (hue < 0.) {
		hsv.x = hue + 1.;
	} else if (hue > 1.) {
		hsv.x = hue - 1.;
	} else {
		hsv.x = hue;
	}
	hsv.y *= 0.5;

	// HSV to RGB
	vec4 k2 = vec4(1., 2./3., 1./3., 3.);
	vec3 p2 = abs(fract(hsv.xxx + k2.xyz) * 6. - k2.www);
	vec3 rgb = hsv.z * mix(k2.xxx, clamp(p2 - k2.xxx, 0., 1.), hsv.y);
	return rgb;
}

// Voronoi method credit:
// The MIT License
// Copyright © 2013 Inigo Quilez
// https://www.shadertoy.com/view/ldl3Dl

vec3 hash(vec3 x) {
	x = vec3(dot(x, vec3(127.1,311.7, 74.7)),
			 dot(x, vec3(269.5,183.3,246.1)),
			 dot(x, vec3(113.5,271.9,124.6)));
	return fract(sin(x) * 43758.5453123);
}

vec2 voronoi(in vec3 x){
	vec3 p = floor(x);
	vec3 f = fract(x);

	float res = 100.;
	float id = 0.;

	for (float k = -1.; k <= 1.; k += 1.) {
		for (float j = -1.; j <= 1.; j += 1.) {
			for (float i = -1.; i <= 1.; i += 1.) {
				vec3 b = vec3(i, j, k);
				vec3 r = vec3(b) - f + hash(p + b);
				float d = dot(r, r);
				if (d < res) {
					res = d;
					id = dot(p + b, vec3(0., 57., 113.));
				}
		    }
		}
	}

	return vec2(sqrt(res), id);
}

void sky() {
	COLOR = sky_color;

	float _sun_distance = 0.0;
	float sun_size = 0.1f, sun_blur = 1.0f;
	vec3 sun_color = vec3(1,.5,.2) * 2.0;
	if( LIGHT0_ENABLED )
	{
		_sun_distance = distance( EYEDIR, LIGHT0_DIRECTION );
		// Bigger sun near the horizon
		float _sun_size = sun_size + cos( LIGHT0_DIRECTION.y * PI ) * sun_size * 0.25;
		// Finding sun disc and edge blur
		float _sun_amount = pow(clamp(( 1.0 - _sun_distance / _sun_size ) / sun_blur, 0.0, 1.0 ), 4.0);
		if( _sun_amount > 0.0 )
		{
			COLOR = mix( COLOR, sun_color, _sun_amount );
		}
	}

	for (int i = 0; i < layers_count; i++) {
		vec3 pos = EYEDIR * (layer_scale + float(i) * layer_scale_step);
		vec2 layer = voronoi(pos);

		vec3 rand = hash(vec3(layer.y));

		#ifdef USE_TWINKLE
			float twinkle = sin(TIME * PI * star_twinkle_speed + rand.x * TAU);
			twinkle *= star_twinkle_intensity;
			float star = smoothstep(star_intensity + star_intensity * twinkle, 0., layer.x);
		#else
			float star = smoothstep(star_intensity, 0., layer.x);
		#endif

		vec3 star_color = star * hue((COLOR + star_base_color), rand.y * star_hue_offset, 1);

		COLOR += star_color;
	}
}