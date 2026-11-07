@tool
extends EditorInspectorPlugin
## The climate plot, at the top of a JarClimateBiomeMap's inspector.

const ClimatePlot := preload("res://addons/jar_voxel_terrain/climate_plot.gd")

var undo: EditorUndoRedoManager

func _can_handle(object: Object) -> bool:
	return object is JarClimateBiomeMap

func _parse_begin(object: Object) -> void:
	var plot: Control = ClimatePlot.new()
	plot.undo = undo
	plot.set_map(object)
	add_custom_control(plot)
