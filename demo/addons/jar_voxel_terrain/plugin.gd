@tool
extends EditorPlugin

const Inspector := preload("res://addons/jar_voxel_terrain/impostor/impostor_inspector.gd")
const PopulationPanel := preload("res://addons/jar_voxel_terrain/population_panel.gd")

var _inspector: EditorInspectorPlugin
var _bake_button: Button
var _panel: VBoxContainer
var _panel_button: Button
var _population: JarVoxelPopulation

func _enter_tree() -> void:
	_inspector = Inspector.new()
	add_inspector_plugin(_inspector)
	_bake_button = Button.new()
	_bake_button.text = "Bake Impostors"
	_bake_button.flat = true
	_bake_button.hide()
	_bake_button.pressed.connect(func():
		if _population != null:
			_inspector.bake(_population, _bake_button))
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, _bake_button)
	_panel = PopulationPanel.new()
	_panel_button = add_control_to_bottom_panel(_panel, "Population")

func _exit_tree() -> void:
	remove_inspector_plugin(_inspector)
	remove_control_from_container(CONTAINER_SPATIAL_EDITOR_MENU, _bake_button)
	_bake_button.queue_free()
	remove_control_from_bottom_panel(_panel)
	_panel.queue_free()

func _handles(object: Object) -> bool:
	return object is JarVoxelPopulation

func _edit(object: Object) -> void:
	if object != null:
		_population = object
		_panel.set_population(_population)

func _make_visible(visible: bool) -> void:
	_bake_button.visible = visible
	if visible:
		make_bottom_panel_item_visible(_panel)
