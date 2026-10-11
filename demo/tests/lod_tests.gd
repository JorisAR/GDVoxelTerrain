extends Node
## Headless checks for level of detail at different voxel sizes, detail
## distances and shell sizes. Run:
##   godot --headless --path demo res://tests/lod_tests.tscn

var _failures := 0

func _ready() -> void:
	for config in [
		{voxel = 1.0, detail = 0.0, shell = 2},
		{voxel = 0.5, detail = 0.0, shell = 2},
		{voxel = 0.25, detail = 0.0, shell = 2},
		{voxel = 0.5, detail = 48.0, shell = 2},
		{voxel = 1.0, detail = 64.0, shell = 1},
	]:
		await _test(config)
	await _test_round_trip()
	print("LOD tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

func _check(name: String, ok: bool, detail := "") -> void:
	if not ok:
		_failures += 1
	print("%s %s %s" % ["PASS" if ok else "FAIL", name, detail])

func _test(config: Dictionary) -> void:
	var label := "voxel %.2f detail %.0f shell %d:" % [config.voxel, config.detail, config.shell]
	var holder := Node.new()
	add_child(holder)
	var world := TestWorld.flat(holder, Vector3(3, 12, 5), func(v: JarVoxelVolume):
		v.voxel_size = config.voxel
		v.lod_detail_distance = config.detail
		v.lod_shell_size = config.shell)
	var volume: JarVoxelVolume = world.volume
	var viewer: Node3D = world.viewer
	await _settle(volume)

	var chunks := []
	for c in volume.get_children():
		if c is JarVoxelChunk and c.visible:
			var size: float = 16.0 * config.voxel * (1 << c.get_lod())
			chunks.append({lod = c.get_lod(), box = AABB(c.position - Vector3.ONE * size * 0.5, Vector3.ONE * size)})
	_check("%s chunks exist" % label, chunks.size() > 50, "%d chunks" % chunks.size())

	# Touching chunks (sharing a face or more) may differ by at most one LOD.
	var violations := 0
	for i in chunks.size():
		var a: AABB = chunks[i].box.grow(0.01)
		for j in range(i + 1, chunks.size()):
			if absi(chunks[i].lod - chunks[j].lod) > 1 and a.intersects(chunks[j].box):
				violations += 1
	_check("%s neighbours within one LOD" % label, violations == 0, "%d violating pairs" % violations)

	# Everything within the detail distance (plus one chunk) is full detail.
	var coarse_near := 0
	for c in chunks:
		var center: Vector3 = c.box.get_center()
		var flat_dist := Vector2(center.x - viewer.position.x, center.z - viewer.position.z).length()
		if flat_dist + c.box.size.x < config.detail and c.lod > 0:
			coarse_near += 1
	_check("%s detail distance is LOD 0" % label, coarse_near == 0, "%d coarse chunks inside" % coarse_near)

	# No cracks: a grid of downward rays around the viewer all hit the ground.
	var space := volume.get_world_3d().direct_space_state
	var misses := 0
	var rays := 0
	var reach: float = 24.0 * config.voxel * 4.0
	var step: float = reach / 40.0
	var x := -reach
	while x <= reach:
		var z := -reach
		while z <= reach:
			var from := viewer.position + Vector3(x, 200, z)
			var hit := space.intersect_ray(PhysicsRayQueryParameters3D.create(from, from + Vector3.DOWN * 400))
			rays += 1
			misses += 0 if hit else 1
			z += step
		x += step
	_check("%s no cracks" % label, misses == 0, "%d of %d rays fell through" % [misses, rays])
	holder.queue_free()
	await get_tree().process_frame

func _settle(volume: JarVoxelVolume) -> void:
	var start := Time.get_ticks_msec()
	var idle := 0
	while idle < 30 and Time.get_ticks_msec() - start < 60000:
		await get_tree().physics_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0
	await get_tree().create_timer(0.5).timeout # colliders upload over a few frames

## Detail the viewer leaves behind is pruned from the octree. Coming back must
## regenerate it, and edits must survive the trip.
func _test_round_trip() -> void:
	var holder := Node.new()
	add_child(holder)
	var world := TestWorld.flat(holder, Vector3(3, 12, 5))
	var volume: JarVoxelVolume = world.volume
	var viewer: Node3D = world.viewer
	await _settle(volume)
	var hole := Vector3(3, 0, 5)
	var ground := _ground_height(volume, hole + Vector3(9, 0, 0))
	var box := JarBoxSdf.new()
	box.extent = Vector3(2, 6, 2)
	volume.edit(box, JarVoxelVolume.SDF_OPERATION_SUBTRACTION, Transform3D(Basis(), Vector3(hole.x, _ground_height(volume, hole), hole.z)))
	await _settle(volume)
	var before := _lod0_count(volume)
	var dug := _ground_height(volume, hole)
	for x in [600.0, 1500.0, 3000.0, 1500.0, 600.0, 3.0]:
		viewer.position.x = x
		await _settle(volume)
	_check("round trip: full detail regenerated", _lod0_count(volume) >= before * 0.9, "%d LOD 0 chunks, %d before" % [_lod0_count(volume), before])
	var dug_after := _ground_height(volume, hole)
	_check("round trip: edit kept", absf(dug_after - dug) < 0.5, "hole floor %.2f, was %.2f" % [dug_after, dug])
	var ground_after := _ground_height(volume, hole + Vector3(9, 0, 0))
	_check("round trip: terrain unchanged", absf(ground_after - ground) < 0.5, "ground %.2f, was %.2f" % [ground_after, ground])
	holder.queue_free()
	await get_tree().process_frame

func _lod0_count(volume: JarVoxelVolume) -> int:
	var n := 0
	for c in volume.get_children():
		n += 1 if c is JarVoxelChunk and c.visible and c.get_lod() == 0 else 0
	return n

func _ground_height(volume: JarVoxelVolume, at: Vector3) -> float:
	var space := volume.get_world_3d().direct_space_state
	var from := Vector3(at.x, 200, at.z)
	var hit := space.intersect_ray(PhysicsRayQueryParameters3D.create(from, from + Vector3.DOWN * 400))
	return hit.position.y if hit else NAN

