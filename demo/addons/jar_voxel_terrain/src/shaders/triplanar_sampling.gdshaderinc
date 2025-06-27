////global uniform sampler2D global_cell_noise;
////global uniform sampler2D global_cell_noise_dist;
//
//vec2 rotateUV(vec2 f_uv, float f_rotation_radians) {
	//float c = cos(f_rotation_radians);
	//float s = sin(f_rotation_radians);
	//return vec2(f_uv.x * c + f_uv.y * s, f_uv.y * c - f_uv.x * s);
//}
//
//vec4 sampleTriplanarRotated(sampler2D f_sampler, vec3 f_global_position, vec3 f_global_normal, float f_scale, float rotation) {
	//return (abs(f_global_normal.x) * texture(f_sampler, rotateUV(f_global_position.yz * f_scale, rotation))
    	//+ abs(f_global_normal.y) * texture(f_sampler, rotateUV(f_global_position.xz * f_scale, rotation))
    	//+ abs(f_global_normal.z) * texture(f_sampler, rotateUV(f_global_position.xy * f_scale, rotation)))
		/// (abs(f_global_normal.x) + abs(f_global_normal.y) + abs(f_global_normal.z));
//}
//
//vec4 sampleTriplanar(sampler2D f_sampler, vec3 f_global_position, vec3 f_global_normal, float f_scale) {
	//return sampleTriplanarRotated(f_sampler, f_global_position, f_global_normal, f_scale, 0.0);
//}
//
//vec4 sampleStochasticTriplanar(sampler2D f_sampler, vec3 f_global_position, vec3 f_global_normal, float f_scale, float f_rotation_scale) {
	//float rotation = sampleTriplanar(global_cell_noise, f_global_position, f_global_normal, f_rotation_scale).r * 2.0 * PI;
	//float mask = sampleTriplanar(global_cell_noise_dist, f_global_position, f_global_normal, f_rotation_scale).r;
//
	//return (1.0 - mask) * sampleTriplanarRotated(f_sampler, f_global_position, f_global_normal, f_scale, 0.0)
		//+ mask * sampleTriplanarRotated(f_sampler, f_global_position, f_global_normal, f_scale, rotation);
//}
//
//vec4 sample_y_planar(sampler2D f_sampler, vec3 f_global_position, float f_scale) {
    //return texture(f_sampler, f_global_position.xz * f_scale);
//}
//
//vec4 sampleFractalYPlanar(sampler2D f_sampler, vec3 f_global_position, float f_scale, vec3 f_cam_pos, vec3 offset) {
	//float d = max(0.5, log2(distance(f_global_position, f_cam_pos)));
    //float s = floor(d);
//
    //float pingpong = float(int(abs(s)) % 2);
//
    //vec2 uvA = f_scale * (f_global_position + offset).xz / exp2(s + pingpong);
    //vec2 uvB = f_scale * (f_global_position + offset).xz / exp2(s + 1.0 - pingpong);
//
    //float fac = fract(d);
    //fac = pingpong < 0.5 ? 1.0 - fac : fac;
//
    //vec4 cA = texture(f_sampler, uvA);
    //vec4 cB = texture(f_sampler, uvB);
//
    //return mix(cB, cA, fac);
//}
//
//vec4 sampleFractalTriPlanar(sampler2D f_sampler, vec3 f_global_position, vec3 f_global_normal, float f_scale, vec3 f_cam_pos) {
	//float d = max(0.5, log2(distance(f_global_position, f_cam_pos)));
    //float s = floor(d);
    //float pingpong = float(int(abs(s)) % 2);
	//float fac = fract(d);
    //fac = pingpong < 0.5 ? 1.0 - fac : fac;
	//float invExp1 = 1.0 / exp2(s + pingpong);
	//float invExp2 = 1.0 / exp2(s + 1.0 - pingpong);
	//vec3 scaledGlobalPos = f_scale * f_global_position;
//
	//vec4 cA = texture(f_sampler, scaledGlobalPos.yz * invExp1);
    //vec4 cB = texture(f_sampler, scaledGlobalPos.yz * invExp2);
	//vec4 x = mix(cB, cA, fac);
//
    //cA = texture(f_sampler, scaledGlobalPos.xz * invExp1);
    //cB = texture(f_sampler, scaledGlobalPos.xz * invExp2);
	//vec4 y = mix(cB, cA, fac);
//
	//cA = texture(f_sampler, scaledGlobalPos.xy * invExp1);
    //cB = texture(f_sampler, scaledGlobalPos.xy * invExp2);
	//vec4 z = mix(cB, cA, fac);
//
	////return fac;
//
	//return (abs(f_global_normal.x) * x + abs(f_global_normal.y) * y + abs(f_global_normal.z) * z)
		/// (abs(f_global_normal.x) + abs(f_global_normal.y) + abs(f_global_normal.z));
//}