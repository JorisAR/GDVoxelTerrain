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
	print("Topology tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

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
