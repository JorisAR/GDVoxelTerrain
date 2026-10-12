// Trampled grass: a texture covering `chunk_size` meters around
// `chunk_center` (xz) whose rgb says how much the grass there is pressed down.
// Without a texture (or a size) nothing is pressed.
uniform sampler2D grass_texture : repeat_disable, hint_default_black;
uniform vec3 chunk_center;
uniform float chunk_size;

vec4 sampleGrassDisplacement(vec3 f_global_pos) {
	if (chunk_size <= 0.0)
		return vec4(0.0);
	vec2 coords = (f_global_pos.xz - chunk_center.xz) / chunk_size + .5;
	return texture(grass_texture, coords);
}
