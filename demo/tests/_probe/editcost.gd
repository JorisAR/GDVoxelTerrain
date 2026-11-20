extends Node

func _settle(volume) -> float:
	var idle := 0
	var ms := 0.0
	while idle < 30:
		await get_tree().process_frame
		ms += JarVoxelStats.get_frame().workers["edit"]
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0
	return ms

func _ready() -> void:
	var world := TestWorld.flat(self, Vector3(0, 40, 0), func(v): v.sdf = load("res://demo/planet/planet_terrain.tres"))
	var volume: JarVoxelVolume = world.volume
	await _settle(volume)
	var ground: float = volume.raycast(Vector3(0, 300, 0), Vector3(0, -300, 0)).position.y
	var x := -100.0
	for r in [8.0, 30.0]:
		for trusted in [true, false]:
			var sphere := JarSphereSdf.new()
			sphere.radius = r
			var shape: JarSignedDistanceField = sphere
			if not trusted:
				shape = JarDisplaceSdf.new()
				shape.sdf = sphere
			volume.edit(shape, JarVoxelVolume.SDF_OPERATION_SUBTRACTION, Transform3D(Basis(), Vector3(x, ground, 0)))
			printerr("EDITMS r=%.0f trusted=%s: %.1f ms" % [r, trusted, await _settle(volume)])
			x += 80.0
	get_tree().quit()
