	//global uniform sampler2D wind_noise_texture;
	//global uniform vec3 wind_dir;
	//global uniform float wind_strength;
//
	//vec3 get_wind_displacement(vec3 global_position, float strength){
		//float time_factor = 0.02;
		//float position_factor = 0.01;
		//vec3 wind = normalize((texture(wind_noise_texture, position_factor * global_position.xz + (time_factor * TIME) * wind_dir.xz).rgb - vec3(.5)) * vec3(1, 0.2, 1));
		//float d = abs(dot(wind, wind_dir));
		//return (strength * d * d * wind_strength) * wind;
	//}