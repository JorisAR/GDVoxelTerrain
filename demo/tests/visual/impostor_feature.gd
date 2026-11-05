extends Node
## Bakes the features of JarVoxelPopulation-style table TABLE (or demo's) and
## saves each impostor; a real run of what the editor's Bake button does.
## SAVE=1 keeps the features pointing at their bakes, otherwise only one feature
## is baked to a copy under user:// to measure time and size.

func _ready() -> void:
	var table: JarPopulationTable = load(OS.get_environment("TABLE"))
	var seen := []
	for row: Dictionary in table.rows:
		if row.asset is JarTerrainFeature and not row.asset in seen:
			seen.append(row.asset)
	for feature: JarTerrainFeature in seen:
		var start := Time.get_ticks_msec()
		var error := await JarImpostorBaker.new().bake_feature(feature, self)
		print(feature.resource_path, " error ", error, " in ", Time.get_ticks_msec() - start, " ms, ",
			FileAccess.get_file_as_bytes(feature.resource_path.get_basename() + "_impostor.res").size() / 1048576, " MB")
	get_tree().quit()
