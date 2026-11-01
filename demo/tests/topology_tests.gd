extends Node
## Headless mesh topology checks around a thin shell (thinner than a voxel),
## for both meshers with manifold mode off and on. Run:
##   godot --headless --path demo res://tests/topology_tests.tscn

var _failures := 0

func _ready() -> void:
	for mesher in [0, 1]:
		for manifold in [false, true]:
			var holder := Node.new()
			add_child(holder)
			var world := TestWorld.flat(holder, Vector3(3, 30, 5), func(v: JarVoxelVolume):
				v.meshing_algorithm = mesher
				v.manifold = manifold)
			var volume: JarVoxelVolume = world.volume
			await _settle(volume)
			# Thin walls: a hollow sphere shell, thinner than a voxel.
			var shell := JarOffsetSdf.new()
			var sphere := JarSphereSdf.new()
			sphere.radius = 6.0
			shell.sdf = sphere
			shell.shell_thickness = 0.6
			volume.edit(shell, JarVoxelVolume.SDF_OPERATION_UNION, Transform3D(Basis(), Vector3(3, 20, 5)))
			await _settle(volume)
			var r := Topology.analyze(volume, Vector3(3, 20, 5), 12.0)
			var label := "%s manifold=%s" % [["surface nets", "dual contouring"][mesher], manifold]
			print("INFO %s %s" % [label, r])
			_check("%s no holes" % label, r.open == 0, "%d open edges" % r.open)
			if manifold:
				_check("%s manifold" % label, r.nonmanifold == 0, "%d non-manifold edges" % r.nonmanifold)
			holder.queue_free()
			await get_tree().process_frame
	await _eroded_mountain("eroded mountain", load("res://tests/planet_terrain_4km.tres"), Vector3(2004.3, 2408.4, 2732.2), 10000.0)
	# The demo planet now (JarNoise ridges): around its highest peak.
	var planet: JarTerrainSdf = load("res://demo/planet/planet_terrain.tres")
	var up := Vector3(-0.2983, 0.9543, 0.0194).normalized()
	await _eroded_mountain("ridged peak", planet, up * (planet.radius + planet.sample_height(up * planet.radius)), 36000.0)
	await _reported_spots()
	await _lod_seams()
	await _moving_seams()
	print("Topology tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

## Steep eroded mountainside of a planet. Its distance field
## overestimates on pinched slopes; the octree used to drop leaves with
## surface in them there (holes), and chunk borders showed in the shading
## (normals averaged from each chunk's own faces).
func _eroded_mountain(label: String, sdf: JarSignedDistanceField, spot: Vector3, extent: float) -> void:
	var holder := Node.new()
	add_child(holder)
	var volume := JarVoxelVolume.new()
	volume.chunk_scene = TestWorld.CHUNK_SCENE
	volume.extent = extent
	volume.sdf = sdf
	volume.meshing_algorithm = JarVoxelVolume.MESHING_DUAL_CONTOURING
	volume.manifold = true
	holder.add_child(volume)
	var viewer := JarVoxelViewer.new()
	viewer.position = spot + spot.normalized() * 4.0
	holder.add_child(viewer)
	await _settle(volume)
	var r := Topology.analyze(volume, spot, 40.0)
	print("INFO %s %s" % [label, r])
	_check("%s no holes" % label, r.open == 0, "%d open edges" % r.open)
	var seams := Topology.seam_normals(volume, spot, 40.0)
	_check("%s chunk borders shade alike" % label, seams.shared > 100 and seams.mismatched * 10 < seams.shared,
		"%d of %d shared vertices differ" % [seams.mismatched, seams.shared])
	holder.queue_free()
	await get_tree().process_frame

## Spots of the demo planet reported from play (tests/report): a steep cliff
## whose distance field overestimated (the octree dropped surface: see-through
## holes), and a ridge whose crest was a crease sharper than a voxel (jagged
## teeth: vertices far off the true surface).
func _reported_spots() -> void:
	for spot: Dictionary in [
		{name = "cliff", viewer = Vector3(2083.2, 3118.1, 2019.6), at = Vector3(2066.2, 3115.9, 2033.8)},
		{name = "ridge", viewer = Vector3(776.9, 4068.7, -657.5), at = Vector3(769.2, 4065.8, -662.9)},
	]:
		var holder := Node.new()
		add_child(holder)
		var volume := JarVoxelVolume.new()
		volume.chunk_scene = TestWorld.CHUNK_SCENE
		volume.extent = 10000.0
		volume.sdf = load("res://tests/planet_terrain_4km.tres")
		volume.meshing_algorithm = JarVoxelVolume.MESHING_DUAL_CONTOURING
		volume.manifold = true
		holder.add_child(volume)
		var viewer := JarVoxelViewer.new()
		viewer.position = spot.viewer
		holder.add_child(viewer)
		await _settle(volume)
		var r := Topology.analyze(volume, spot.at, 30.0)
		var q := Topology.quality(volume, spot.at, 30.0, true)
		_check("reported %s no holes" % spot.name, r.open == 0, "%d open edges" % r.open)
		_check("reported %s on the surface" % spot.name, q.off_surface == 0 and q.flipped * 1000 < q.triangles,
			"%d vertices off the surface, %d of %d faces folded" % [q.off_surface, q.flipped, q.triangles])
		holder.queue_free()
		await get_tree().process_frame

## Where chunks of different LODs meet, from the viewer out to a kilometer:
## not a single open edge (the mesh is closed across every seam, with dual
## contouring and with surface nets, manifold or not).
func _lod_seams() -> void:
	var sdf: JarTerrainSdf = load("res://tests/planet_terrain_4km.tres")
	for setup in [[JarVoxelVolume.MESHING_DUAL_CONTOURING, true], [JarVoxelVolume.MESHING_DUAL_CONTOURING, false],
			[JarVoxelVolume.MESHING_SURFACE_NETS, false]]:
		var holder := Node.new()
		add_child(holder)
		var volume := JarVoxelVolume.new()
		volume.chunk_scene = TestWorld.CHUNK_SCENE
		volume.extent = 10000.0
		volume.sdf = sdf
		volume.meshing_algorithm = setup[0]
		volume.manifold = setup[1]
		holder.add_child(volume)
		var up := Vector3(0.45, 0.55, 0.7).normalized()
		var viewer := JarVoxelViewer.new()
		viewer.position = up * (sdf.radius + sdf.sample_height(up * sdf.radius) + 5.0)
		holder.add_child(viewer)
		await _settle(volume)
		# Spots along the ground (in the volume's space, as Topology takes
		# them), across the LOD 0-1, 1-2 and 2-3 seams.
		var side := up.cross(Vector3.UP).normalized()
		var open := 0
		var spots := 0
		var triangles := 0
		var lods := {}
		for k in range(1, 40):
			var dir := (up * sdf.radius + side * (k * 25.0)).normalized()
			var at := dir * (sdf.radius + sdf.sample_height(dir * sdf.radius))
			var r := Topology.analyze(volume, at, 30.0)
			open += r.open
			triangles += r.triangles
			spots += 1
		for c in volume.get_children():
			if c is JarVoxelChunk and c.visible:
				lods[c.get_lod()] = true
		var label := "%s%s" % [["surface nets", "dual contouring"][setup[0]], " manifold" if setup[1] else ""]
		_check("%s: LOD seams closed" % label, open == 0 and lods.size() >= 3 and triangles > 100000,
			"%d open edges in %d triangles at %d spots, LODs %s" % [open, triangles, spots, lods.keys()])
		holder.queue_free()
		await get_tree().process_frame

## While the viewer flies, LOD updates are shown in groups as they finish
## (see MeshComputeScheduler): what is on screen at any frame must be closed
## across every seam, old and new chunks side by side. Checked between frames
## at spots 30-300 m from the viewer (across the LOD 0-1, 1-2 and 2-3 seams).
func _moving_seams() -> void:
	var sdf: JarTerrainSdf = load("res://tests/planet_terrain_4km.tres")
	var holder := Node.new()
	add_child(holder)
	var volume := JarVoxelVolume.new()
	volume.chunk_scene = TestWorld.CHUNK_SCENE
	volume.extent = 10000.0
	volume.sdf = sdf
	volume.meshing_algorithm = JarVoxelVolume.MESHING_DUAL_CONTOURING
	holder.add_child(volume)
	var up := Vector3(0.45, 0.55, 0.7).normalized()
	var axis := up.cross(Vector3.UP).normalized()
	var viewer := JarVoxelViewer.new()
	viewer.position = up * (sdf.radius + sdf.sample_height(up * sdf.radius) + 5.0)
	holder.add_child(viewer)
	await _settle(volume)
	var open := 0
	var checks := 0
	var commits := 0
	var worst := []
	var angle := 0.0
	var frame := -1
	var start := Time.get_ticks_msec()
	var batches := 0
	var was_building := false
	while Time.get_ticks_msec() - start < 12000:
		frame += 1
		# Like flying at ~500 m/s, but waiting for builds: headless frames
		# are fast, and a viewer racing ahead made each build regenerate
		# kilometres (seconds each).
		if not volume.is_building():
			angle += 8.0 / sdf.radius
		var dir := up.rotated(axis, angle)
		# Through the volume: the floating origin moves it (the surface is
		# farther out than the origin's reach).
		viewer.global_position = volume.to_global(dir * (sdf.radius + sdf.sample_height(dir * sdf.radius) + 5.0))
		await get_tree().process_frame
		commits += JarVoxelStats.get_frame().counts["chunks committed"]
		if volume.is_building() and not was_building:
			batches += 1
		was_building = volume.is_building()
		if frame % 20 != 0:
			continue
		var side := dir.cross(axis).normalized()
		for d in [30.0, 80.0, 150.0, 300.0]:
			for s in [1.0 if frame % 40 == 0 else -1.0]:
				var spot: Vector3 = (dir * sdf.radius + side * (s * d)).normalized()
				var r := Topology.analyze(volume, spot * (sdf.radius + sdf.sample_height(spot * sdf.radius)), 20.0)
				open += r.open
				checks += 1
				if r.open > 0 and worst.size() < 4:
					worst.append([frame, d, r.open_at.slice(0, 2)])
	_check("moving: seams closed on every frame", open == 0 and batches >= 3,
		"%d open edges over %d checks, %d LOD updates (%d chunks) shown meanwhile %s" % [open, checks, batches, commits, worst])
	holder.queue_free()
	await get_tree().process_frame

func _check(name: String, ok: bool, detail := "") -> void:
	if not ok:
		_failures += 1
	print("%s %s %s" % ["PASS" if ok else "FAIL", name, detail])

func _settle(volume) -> void:
	var start := Time.get_ticks_msec()
	var idle := 0
	while idle < 30 and Time.get_ticks_msec() - start < 60000:
		await get_tree().process_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0
