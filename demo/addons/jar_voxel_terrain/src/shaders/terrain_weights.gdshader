shader_type spatial;
// JarVoxelVolume terrain shader for MATERIAL_MODE_WEIGHTS: materials 0-7 blend
// smoothly by per-vertex weights (COLOR = materials 0-3, CUSTOM0 = 4-7);
// unpainted ground follows the ground rules. See terrain_common.gdshaderinc.
#include "res://addons/jar_voxel_terrain/src/shaders/terrain_common.gdshaderinc"

varying vec4 v_weights_high;

void vertex() {
	JAR_VERTEX
	v_weights_high = CUSTOM0;
}

void fragment() {
	JarContext c = jar_context(jar_position, jar_normal, -VERTEX.z);
	JarSurface s = JarSurface(vec3(0.0), vec3(0.0), 0.0, 0.0);
	vec4 lo = COLOR;
	vec4 hi = v_weights_high;
	float painted = lo.r + lo.g + lo.b + lo.a + hi.r + hi.g + hi.b + hi.a;
	float scale = painted > 1.0 ? 1.0 / painted : 1.0;
	jar_add_material(s, c, 0, lo.r * scale);
	jar_add_material(s, c, 1, lo.g * scale);
	jar_add_material(s, c, 2, lo.b * scale);
	jar_add_material(s, c, 3, lo.a * scale);
	jar_add_material(s, c, 4, hi.r * scale);
	jar_add_material(s, c, 5, hi.g * scale);
	jar_add_material(s, c, 6, hi.b * scale);
	jar_add_material(s, c, 7, hi.a * scale);
	jar_add_ground(s, c, 1.0 - clamp(painted, 0.0, 1.0));
	jar_finish(s, c, MODEL_MATRIX, VIEW_MATRIX, ALBEDO, ROUGHNESS, NORMAL);
	SPECULAR = 0.25;
}
