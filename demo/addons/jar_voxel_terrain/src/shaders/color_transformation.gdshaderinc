vec3 rgb2hsv(vec3 f_color) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(f_color.bg, K.wz), vec4(f_color.gb, K.xy), step(f_color.b, f_color.g));
    vec4 q = mix(vec4(p.xyw, f_color.r), vec4(f_color.r, p.yzx), step(p.x, f_color.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 f_color) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(f_color.xxx + K.xyz) * 6.0 - K.www);
    return f_color.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), f_color.y);
}

vec3 noiseColorTransformer(vec3 f_color, vec3 f_noise) {
	vec3 hsv_col = rgb2hsv(f_color);
	hsv_col = vec3(hsv_col.x, hsv_col.y * (f_noise.y * 0.2 + 0.8), hsv_col.z * (f_noise.z * 0.4 + 0.6));
	return hsv2rgb(hsv_col);
}

vec3 noiseNormalTransformer(vec3 f_normal, vec3 f_noise, float f_strength) {
	vec3 noise_vec = normalize(f_noise * 2.0 - vec3(1.0));
	return normalize(f_normal + noise_vec * f_strength);
}