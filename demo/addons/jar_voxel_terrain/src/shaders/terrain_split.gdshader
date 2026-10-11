shader_type spatial;
// JarVoxelVolume terrain shader for MATERIAL_MODE_SPLIT: one material per
// triangle (id in CUSTOM0.r, 255 = ground), crisp borders, up to 255 materials.
// See terrain_common.gdshaderinc.
#include "res://addons/jar_voxel_terrain/src/shaders/terrain_common.gdshaderinc"

varying flat int v_material;

void vertex() {
	JAR_VERTEX
	v_material = int(round(CUSTOM0.r * 255.0));
}

void fragment() {
	JarContext c = jar_context(jar_position, jar_normal, jar_biomes, -VERTEX.z);
	JarSurface s = JarSurface(vec3(0.0), vec3(0.0), 0.0, 0.0);
	if (v_material == 255)
		jar_add_ground(s, c, 1.0);
	else
		jar_add_material(s, c, v_material, 1.0);
	jar_finish(s, c, MODEL_MATRIX, VIEW_MATRIX, ALBEDO, ROUGHNESS, NORMAL);
	SPECULAR = 0.25;
}
