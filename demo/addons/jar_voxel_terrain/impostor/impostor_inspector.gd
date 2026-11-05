@tool
extends EditorInspectorPlugin
## A Bake button on JarTerrainFeature, and Bake all on JarVoxelPopulation.

func _can_handle(object: Object) -> bool:
	return object is JarTerrainFeature or object is JarVoxelPopulation

func _parse_begin(object: Object) -> void:
	var button := Button.new()
	button.text = "Bake impostor" if object is JarTerrainFeature else "Bake all impostors"
	button.pressed.connect(bake.bind(object, button))
	add_custom_control(button)

func _features(object: Object) -> Array[JarTerrainFeature]:
	var result: Array[JarTerrainFeature] = []
	if object is JarTerrainFeature:
		result.append(object)
	elif object.table != null:
		for row: Dictionary in object.table.rows:
			if row.get("asset") is JarTerrainFeature and not row.asset in result:
				result.append(row.asset)
	return result

## Also the 3D editor's toolbar button.
func bake(object: Object, button: Button) -> void:
	var text := button.text
	button.disabled = true
	var features := _features(object)
	var parent := EditorInterface.get_base_control()
	var folder := EditorInterface.get_edited_scene_root().scene_file_path.get_base_dir() if EditorInterface.get_edited_scene_root() else "res://"
	var failed := 0
	for i in features.size():
		var baker := JarImpostorBaker.new()
		baker.progress.connect(func(amount: float):
			if is_instance_valid(button):
				button.text = "Baking %d of %d: %d%%" % [i + 1, features.size(), roundi(amount * 100.0)])
		if await baker.bake_feature(features[i], parent, folder) != OK:
			failed += 1
	if failed > 0:
		push_warning("Impostors: %d of %d features could not be baked." % [failed, features.size()])
	EditorInterface.get_resource_filesystem().scan()
	var root := EditorInterface.get_edited_scene_root()
	if root != null:
		for population: JarVoxelPopulation in root.find_children("*", "JarVoxelPopulation", true, false):
			population.regenerate()
	if is_instance_valid(button):
		button.text = text
		button.disabled = false
