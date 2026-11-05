@tool
extends EditorPlugin
## With a JarVoxelPopulation selected: buttons next to the 3D editor's View menu
## to add layers from files and to bake impostors.

const Inspector := preload("res://addons/jar_voxel_terrain/impostor/impostor_inspector.gd")
const LayerFiles := preload("res://addons/jar_voxel_terrain/layer_files.gd")

var _inspector: EditorInspectorPlugin
var _add_button: Button
var _bake_button: Button
var _file_dialog: EditorFileDialog
var _population: JarVoxelPopulation

func _enter_tree() -> void:
	_inspector = Inspector.new()
	add_inspector_plugin(_inspector)
	_add_button = _menu_button("Add Layers", _pick_files)
	_bake_button = _menu_button("Bake Impostors", func():
		if _population != null:
			_inspector.bake(_population, _bake_button))
	_file_dialog = EditorFileDialog.new()
	_file_dialog.file_mode = EditorFileDialog.FILE_MODE_OPEN_FILES
	_file_dialog.access = EditorFileDialog.ACCESS_RESOURCES
	_file_dialog.title = "Add layers: scenes, meshes, features or details"
	for filter in LayerFiles.FILTERS:
		_file_dialog.add_filter(filter)
	_file_dialog.files_selected.connect(_add_files)
	EditorInterface.get_base_control().add_child(_file_dialog)

func _exit_tree() -> void:
	remove_inspector_plugin(_inspector)
	for button: Button in [_add_button, _bake_button]:
		remove_control_from_container(CONTAINER_SPATIAL_EDITOR_MENU, button)
		button.queue_free()
	_file_dialog.queue_free()

func _menu_button(text: String, pressed: Callable) -> Button:
	var button := Button.new()
	button.text = text
	button.flat = true
	button.hide()
	button.pressed.connect(pressed)
	add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, button)
	return button

func _handles(object: Object) -> bool:
	return object is JarVoxelPopulation

func _edit(object: Object) -> void:
	if object != null:
		_population = object

func _make_visible(visible: bool) -> void:
	_add_button.visible = visible
	_bake_button.visible = visible

func _pick_files() -> void:
	_file_dialog.popup_file_dialog()

func _add_files(paths: PackedStringArray) -> void:
	if _population == null:
		return
	var root := EditorInterface.get_edited_scene_root()
	var undo := get_undo_redo()
	undo.create_action("Add population layers")
	var made := 0
	for path in paths:
		var layer := LayerFiles.layer_for(path)
		if layer == null:
			push_warning("Population: cannot grow %s." % path)
			continue
		undo.add_do_method(_population, "add_child", layer, true)
		undo.add_do_method(layer, "set_owner", root)
		undo.add_do_reference(layer)
		undo.add_undo_method(_population, "remove_child", layer)
		made += 1
	undo.commit_action()
	if made > 0:
		EditorInterface.get_resource_filesystem().scan()
