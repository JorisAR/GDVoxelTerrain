@tool
extends EditorPlugin

const Inspector := preload("res://addons/jar_voxel_terrain/impostor/impostor_inspector.gd")

var _inspector: EditorInspectorPlugin

func _enter_tree() -> void:
	_inspector = Inspector.new()
	add_inspector_plugin(_inspector)

func _exit_tree() -> void:
	remove_inspector_plugin(_inspector)
