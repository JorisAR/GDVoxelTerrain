const mat4 THRESHOLD_MATRIX = mat4(
		vec4(1.0 / 17.0,  9.0 / 17.0,  3.0 / 17.0, 11.0 / 17.0),
		vec4(13.0 / 17.0,  5.0 / 17.0, 15.0 / 17.0,  7.0 / 17.0),
		vec4(4.0 / 17.0, 12.0 / 17.0,  2.0 / 17.0, 10.0 / 17.0),
		vec4(16.0 / 17.0,  8.0 / 17.0, 14.0 / 17.0,  6.0 / 17.0));
//input: SCREEN_UV * VIEWPORT_SIZE, intensity
//discard if true, e.g. if(dither(SCREEN_UV * VIEWPORT_SIZE)) discard;
bool dither(vec2 f_uv, float f_intensity) {
	return THRESHOLD_MATRIX[int(f_uv.x) % 4][int(f_uv.y) % 4] <= f_intensity;
}