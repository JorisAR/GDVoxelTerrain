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
	await _test_fast_flight()
	print("LOD tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

## Flying fast over the demo planet: the LOD changes by several levels between
## builds, and a chunk must stay until the chunks replacing it (any number of
## levels finer or coarser) are on screen, or the terrain flashes holes.
## Afterwards no replaced chunk may linger on top of its replacement.
func _test_fast_flight() -> void:
	var holder := Node.new()
	add_child(holder)
	var sdf: JarSignedDistanceField = load("res://demo/planet/planet_terrain.tres")
	var volume := JarVoxelVolume.new()
	volume.chunk_scene = TestWorld.CHUNK_SCENE
	volume.extent = 10000.0
	volume.sdf = sdf
	volume.lod_automatic_update_distance = 16.0
	holder.add_child(volume)
	var viewer := JarVoxelViewer.new()
	var start := Vector3(0.45, 0.55, 0.7).normalized()
	viewer.position = start * 4300.0
	holder.add_child(viewer)
	await _settle(volume)
	var axis := start.cross(Vector3.UP).normalized()
	var rng := RandomNumberGenerator.new()
	rng.seed = 1
	var holes := 0
	var worst := 0
	var mismatched_frames := 0
	for f in 400:
		await get_tree().process_frame
		var dir := start.rotated(axis, 250.0 * (f + 1) / 60.0 / 4300.0) # 250 m/s
		viewer.position = dir * 4260.0
		var boxes := _chunk_boxes(volume, true)
		var missing := 0
		for i in 40:
			# A surface point within 300 m; coarse chunks may place their surface
			# up to a voxel (of their LOD) off the true one.
			var q := (dir * 4000.0 + Vector3(rng.randf_range(-1, 1), rng.randf_range(-1, 1), rng.randf_range(-1, 1)) * 300.0).normalized()
			var lo := 3700.0
			var hi := 4600.0
			for k in 30:
				var m := (lo + hi) * 0.5
				if sdf.sample(q * m) < 0.0: lo = m
				else: hi = m
			if not boxes.any(func(b): return (b[0] as AABB).has_point(q * lo)):
				missing += 1
		holes += 1 if missing > 0 else 0
		worst = maxi(worst, missing)
		if f % 8 == 7:
			mismatched_frames += 1 if _seam_mismatches(volume) > 0 else 0
	_check("fast flight no holes", holes == 0, "%d of 400 frames, up to %d of 40 points uncovered" % [holes, worst])
	_check("fast flight seams match their neighbors", mismatched_frames == 0,
		"%d of 50 checks with chunks meshed for another neighbor LOD than the one shown" % mismatched_frames)
	await _settle(volume)
	var visible := _chunk_boxes(volume, false)
	var nested := 0
	for a in visible:
		for b in visible:
			if a[1] > b[1] and (a[0] as AABB).grow(-0.01).has_point((b[0] as AABB).get_center()):
				nested += 1
	_check("fast flight leaves no replaced chunks", nested == 0, "%d chunks inside coarser ones" % nested)
	holder.queue_free()
	await get_tree().process_frame

## Faces of shown chunks whose seam was meshed for another LOD than that of the
## chunk shown across it: a crack. Chunks that change LOD change their
## neighbors' seams, so a build's meshes must all be shown together.
func _seam_mismatches(volume: JarVoxelVolume) -> int:
	const FACES := [Vector3(1, 0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0), Vector3(0, -1, 0), Vector3(0, 0, 1), Vector3(0, 0, -1)]
	var shown := {}
	var chunks := []
	for c in volume.get_children():
		if c is JarVoxelChunk and c.visible and c.get_mesh_instance().mesh and c.get_mesh_instance().mesh.get_surface_count() > 0:
			var size: float = 16.0 * volume.voxel_size * pow(2.0, c.get_lod())
			shown[_chunk_key(c.position, c.get_lod(), size)] = true
			chunks.append(c)
	var bad := 0
	for c: JarVoxelChunk in chunks:
		var lod := c.get_lod()
		var size: float = 16.0 * volume.voxel_size * pow(2.0, lod)
		var bits := c.get_boundaries()
		for i in 6:
			var p: Vector3 = c.position + FACES[i] * (size * 0.5 + 0.25 * volume.voxel_size)
			var other := -1
			for l in 9:
				if shown.has(_chunk_key(p, l, 16.0 * volume.voxel_size * pow(2.0, l))):
					other = l
					break
			if other >= 0 and ((other > lod) != bool(bits & (1 << i)) or (other < lod) != bool(bits & (1 << (i + 8)))):
				bad += 1
	return bad

func _chunk_key(p: Vector3, lod: int, size: float) -> String:
	return "%d:%d:%d:%d" % [lod, floori(p.x / size), floori(p.y / size), floori(p.z / size)]

## [box, LOD] of each visible chunk with a mesh; `tolerant` grows each by one
## of its voxels.
func _chunk_boxes(volume: JarVoxelVolume, tolerant: bool) -> Array:
	var boxes := []
	for c in volume.get_children():
		if c is JarVoxelChunk and c.visible and c.get_mesh_instance().mesh and c.get_mesh_instance().mesh.get_surface_count() > 0:
			var voxel: float = volume.voxel_size * pow(2.0, c.get_lod())
			var box := AABB(c.position - Vector3.ONE * 8.0 * voxel, Vector3.ONE * 16.0 * voxel)
			boxes.append([box.grow(voxel) if tolerant else box, c.get_lod()])
	return boxes

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
			# Off the voxel grid: a ray exactly through a mesh vertex can slip
			# between triangles in Jolt (no gap in the mesh).
			var from := viewer.position + Vector3(x + 0.0137, 200, z + 0.0073)
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

