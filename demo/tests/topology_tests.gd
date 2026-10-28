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
	await _eroded_mountain()
	await _reported_spots()
	print("Topology tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

## Steep eroded mountainside of the demo planet. Its distance field
## overestimates on pinched slopes; the octree used to drop leaves with
## surface in them there (holes), and chunk borders showed in the shading
## (normals averaged from each chunk's own faces).
func _eroded_mountain() -> void:
	var holder := Node.new()
	add_child(holder)
	var spot := Vector3(2004.3, 2408.4, 2732.2)
	var volume := JarVoxelVolume.new()
	volume.chunk_scene = TestWorld.CHUNK_SCENE
	volume.extent = 10000.0
	volume.sdf = load("res://demo/planet/planet_terrain.tres")
	volume.meshing_algorithm = JarVoxelVolume.MESHING_DUAL_CONTOURING
	volume.manifold = true
	holder.add_child(volume)
	var viewer := JarVoxelViewer.new()
	viewer.position = spot + spot.normalized() * 4.0
	holder.add_child(viewer)
	await _settle(volume)
	var r := Topology.analyze(volume, spot, 40.0)
	print("INFO eroded mountain %s" % r)
	_check("eroded mountain no holes", r.open == 0, "%d open edges" % r.open)
	var seams := Topology.seam_normals(volume, spot, 40.0)
	_check("eroded mountain chunk borders shade alike", seams.shared > 100 and seams.mismatched * 10 < seams.shared,
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
		volume.sdf = load("res://demo/planet/planet_terrain.tres")
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
