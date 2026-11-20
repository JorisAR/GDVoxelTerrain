extends Node
func _ready() -> void:
	for p in ["res://addons/jar_voxel_terrain/nodes/rings/gas_giant.gdshader", "res://demo/assets/population/Trees/Tree.gdshader", "res://demo/assets/population/Grass/Grass2.gdshader", "res://addons/jar_voxel_terrain/shaders/impostor.gdshader", "res://addons/jar_voxel_terrain/nodes/clouds/clouds.gdshader", "res://addons/jar_voxel_terrain/nodes/rings/rings.gdshader", "res://addons/jar_voxel_terrain/src/shaders/terrain_weights.gdshader", "res://addons/jar_voxel_terrain/src/shaders/terrain_split.gdshader", "res://addons/jar_voxel_terrain/src/shaders/terrain_indexed.gdshader"]:
		print(p.get_file(), " ", (load(p) as Shader).get_shader_uniform_list().size())
	get_tree().quit()
