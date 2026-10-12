mat4 get_billboard_MODELVIEW(mat4 f_MODEL_MATRIX, mat4 f_VIEW_MATRIX, mat4 f_INV_VIEW_MATRIX) {
	float scaleX = length(vec3(f_MODEL_MATRIX[0].x, f_MODEL_MATRIX[1].x, f_MODEL_MATRIX[2].x));
	float scaleY = length(vec3(f_MODEL_MATRIX[0].y, f_MODEL_MATRIX[1].y, f_MODEL_MATRIX[2].y));

    return f_VIEW_MATRIX * mat4(
        f_INV_VIEW_MATRIX[0] * scaleX,
        f_INV_VIEW_MATRIX[1] * scaleY,
        f_INV_VIEW_MATRIX[2],
        f_MODEL_MATRIX[3]
    );
}

mat4 get_billboard_MODELVIEW_Y(mat4 f_MODEL_MATRIX, mat4 f_VIEW_MATRIX, mat4 f_INV_VIEW_MATRIX) {
    float scaleX = length(vec3(f_MODEL_MATRIX[0].x, f_MODEL_MATRIX[1].x, f_MODEL_MATRIX[2].x));
    float scaleY = length(vec3(f_MODEL_MATRIX[0].y, f_MODEL_MATRIX[1].y, f_MODEL_MATRIX[2].y));

    // Extract the rotation part of the inverse view matrix
    mat4 rotationY = mat4(
        vec4(f_INV_VIEW_MATRIX[0].x, 0.0, f_INV_VIEW_MATRIX[0].z, 0.0),
        vec4(0.0, 1.0, 0.0, 0.0),
        vec4(f_INV_VIEW_MATRIX[2].x, 0.0, f_INV_VIEW_MATRIX[2].z, 0.0),
        vec4(0.0, 0.0, 0.0, 1.0)
    );

    return f_VIEW_MATRIX * mat4(
        rotationY[0] * scaleX,
        rotationY[1] * scaleY,
        rotationY[2],
        f_MODEL_MATRIX[3]
    );
}

vec3 sample_volume_billboard(vec2 _uv, float _uv_scale, mat4 _inv_view_matrix) {
	return vec3(0.5) - (_inv_view_matrix * vec4(_uv_scale * vec3(1.0-_uv.x, _uv.y, 0.5) - vec3(0.5), 0)).xyz;
}
