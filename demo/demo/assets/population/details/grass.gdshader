shader_type spatial;
render_mode cull_disabled, world_vertex_coords;
uniform vec3 grass_color: source_color = vec3(0.8, 1.0, 0.3);

void vertex() {
	// Called for every vertex the material is visible on.
}

void fragment() {
	// Called for every pixel the material is visible on.
	NORMAL = FRONT_FACING ? NORMAL : -NORMAL;
	ALBEDO = grass_color;
	SPECULAR = 0.1;
	ROUGHNESS = 0.8;	
}

//void light() {
//	// Called for every pixel for every light affecting the material.
//	// Uncomment to replace the default light processing function with this one.
//}
