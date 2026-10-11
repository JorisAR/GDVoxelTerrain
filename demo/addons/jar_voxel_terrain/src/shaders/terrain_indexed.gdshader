shader_type spatial;
// JarVoxelVolume terrain shader for MATERIAL_MODE_INDEXED: per triangle the 4
// strongest of up to 255 materials (ids in CUSTOM0, the same on all corners),
// blended smoothly by per-vertex weights (COLOR); the rest is ground.
// See terrain_common.gdshaderinc.
#include "res://addons/jar_voxel_terrain/src/shaders/terrain_common.gdshaderinc"

varying flat ivec4 v_ids;

void vertex() {
	JAR_VERTEX
	v_ids = ivec4(round(CUSTOM0 * 255.0));
}

void fragment() {
	JarContext c = jar_context(jar_position, jar_normal, -VERTEX.z);
	JarSurface s = JarSurface(vec3(0.0), vec3(0.0), 0.0, 0.0);
	vec4 w = COLOR;
	float painted = w.r + w.g + w.b + w.a;
	float scale = painted > 1.0 ? 1.0 / painted : 1.0;
	jar_add_material(s, c, v_ids.x, w.r * scale);
	jar_add_material(s, c, v_ids.y, w.g * scale);
	jar_add_material(s, c, v_ids.z, w.b * scale);
	jar_add_material(s, c, v_ids.w, w.a * scale);
	jar_add_ground(s, c, 1.0 - clamp(painted, 0.0, 1.0));
	jar_finish(s, c, MODEL_MATRIX, VIEW_MATRIX, ALBEDO, ROUGHNESS, NORMAL);
	SPECULAR = 0.25;
}
