extends Node
## Bakes the features of POPULATION (a scene with a JarVoxelPopulation at its
## root) and saves each impostor: what the editor's Bake button does.

func _ready() -> void:
	var population: JarVoxelPopulation = load(OS.get_environment("POPULATION")).instantiate()
	var seen := []
	for layer: JarPopulationLayer in population.get_layers():
		if layer is JarFeatureLayer and layer.feature != null and not layer.feature in seen:
			seen.append(layer.feature)
	population.free()
	for feature: JarTerrainFeature in seen:
		var start := Time.get_ticks_msec()
		var error := await JarImpostorBaker.new().bake_feature(feature, self)
		print(feature.resource_path, " error ", error, " in ", Time.get_ticks_msec() - start, " ms, ",
			FileAccess.get_file_as_bytes(feature.resource_path.get_basename() + "_impostor.res").size() / 1048576, " MB")
	get_tree().quit()
