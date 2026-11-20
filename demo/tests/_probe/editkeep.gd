extends Node

func _settle(volume) -> void:
	var idle := 0
	while idle < 30:
		await get_tree().process_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0

func _ready() -> void:
	var dig := OS.get_environment("DIG") == "1"
	var world := TestWorld.flat(self, Vector3(0, 40, 0))
	var volume: JarVoxelVolume = world.volume
	var viewer: Node3D = world.viewer
	await _settle(volume)
	var ground: float = volume.raycast(Vector3(0, 300, 0), Vector3(0, -300, 0)).position.y
	if dig:
		volume.sphere_edit(Vector3(0, ground, 0), 1.5, false)
		await _settle(volume)
	printerr("MARK near")
	for step in 8:
		viewer.global_position = volume.to_global(Vector3(600.0 * (step + 1), 40, 0))
		await _settle(volume)
	printerr("MARK far")
	viewer.global_position = volume.to_global(Vector3(4801, 40, 0))
	await _settle(volume)
	await _settle(volume)
	get_tree().quit()
