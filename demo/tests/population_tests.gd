extends Node
## Headless checks for population: features (persistent, on the SDF surface,
## removed by edits) and details (scattered over full-detail chunks). Run:
##   godot --headless --path demo res://tests/population_tests.tscn

var _failures := 0

func _ready() -> void:
	await _test_features()
	await _test_details()
	print("Population tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

func _check(name: String, ok: bool, detail := "") -> void:
	if not ok:
		_failures += 1
	print("%s %s %s" % ["PASS" if ok else "FAIL", name, detail])

func _box_feature(density: float) -> JarTerrainFeature:
	var f := JarTerrainFeature.new()
	var m := BoxMesh.new()
	m.size = Vector3(1, 4, 1)
	f.mesh = m
	f.density = density
	f.draw_distance = 200.0
	f.maximum_slope = 90.0
	return f

func _test_features() -> void:
	var feature := _box_feature(0.01)
	var world := TestWorld.flat(self, Vector3(3, 30, 5), func(v: JarVoxelVolume):
		v.population_terrain_features = [feature])
	var volume: JarVoxelVolume = world.volume
	var viewer: Node3D = world.viewer
	await _settle(volume)
	var center := Vector3(viewer.position.x, 0, viewer.position.z)
	var found := volume.get_features_near(center, 150.0)
	_check("features generated", found.size() > 0, "%d within 150 m" % found.size())

	# On the surface (the flat test terrain is a heightfield: compare heights).
	var worst := 0.0
	for f in found:
		var t: Transform3D = f.transform
		worst = maxf(worst, absf(volume.sdf.sample(volume.to_local(t.origin))))
	_check("features on the surface", worst < 0.2, "largest |sdf| at a base: %.3f" % worst)

	# Density: about density * area on gentle ground (the 2D distance matters
	# on a heightfield; the 3D query radius slightly undercounts slopes).
	var expected := 0.01 * PI * 100.0 * 100.0
	var count := volume.get_features_near(center, 100.0).size()
	_check("feature density", count > expected * 0.6 and count < expected * 1.4, "%d within 100 m, expected ~%.0f" % [count, expected])

	# Rendered: multimesh instances under the volume.
	var rendered := 0
	for n in volume.find_children("*", "MultiMeshInstance3D", true, false):
		rendered += n.multimesh.instance_count
	_check("features rendered", rendered >= found.size(), "%d instances in multimeshes" % rendered)

	# Deterministic: regenerating gives the same features.
	var before := _origins(volume.get_features_near(center, 60.0))
	volume.regenerate_population()
	await _settle(volume)
	var after := _origins(volume.get_features_near(center, 60.0))
	_check("features deterministic", before == after and before.size() > 0, "%d before, %d after" % [before.size(), after.size()])

	# An edit removes the features it touches, also after regenerating.
	var target: Vector3 = volume.get_features_near(center, 60.0)[0].transform.origin
	volume.sphere_edit(target, 3.0, false)
	await _settle(volume)
	_check("edit removes feature", volume.get_features_near(target, 0.5).is_empty())
	volume.regenerate_population()
	await _settle(volume)
	_check("removal persists", volume.get_features_near(target, 0.5).is_empty())

	# remove_features() for gameplay (e.g. chopping a tree).
	var other: Vector3 = volume.get_features_near(center + Vector3(40, 0, 0), 60.0)[0].transform.origin
	volume.remove_features(other, 0.5)
	_check("remove_features", volume.get_features_near(other, 0.5).is_empty())

	# Rules: nothing passes a 0 degree slope limit on noise terrain, nor a height
	# range above the terrain; a mask removes about half.
	feature.maximum_slope = 0.0
	volume.regenerate_population()
	await _settle(volume)
	_check("slope rule", volume.get_features_near(center, 100.0).size() == 0)
	feature.maximum_slope = 90.0
	feature.minimum_height = 500.0
	volume.regenerate_population()
	await _settle(volume)
	_check("height rule", volume.get_features_near(center, 100.0).size() == 0)
	feature.minimum_height = -100000.0
	var mask := FastNoiseLite.new()
	mask.frequency = 0.02
	feature.mask_noise = mask
	feature.mask_threshold = 0.0
	volume.regenerate_population()
	await _settle(volume)
	var masked := volume.get_features_near(center, 100.0).size()
	_check("mask rule", masked > count * 0.2 and masked < count * 0.8, "%d of %d kept" % [masked, count])

	world.volume.queue_free()
	world.viewer.queue_free()
	await get_tree().process_frame

func _test_details() -> void:
	var detail := JarTerrainDetail.new()
	detail.mesh = BoxMesh.new()
	detail.density = 2.0
	detail.maximum_slope = 90.0
	var world := TestWorld.flat(self, Vector3(3, 30, 5), func(v: JarVoxelVolume):
		v.population_terrain_details = [detail])
	var volume: JarVoxelVolume = world.volume
	await _settle(volume)
	var origins := {}
	var total := 0
	for c in volume.get_children():
		if not (c is JarVoxelChunk and c.visible):
			continue
		for n in c.get_children():
			if n is MultiMeshInstance3D and n.multimesh:
				for i in n.multimesh.instance_count:
					total += 1
					origins[(c.position + n.multimesh.get_instance_transform(i).origin).snapped(Vector3.ONE * 0.001)] = true
	_check("details generated", total > 1000, "%d details" % total)
	# The headless dummy renderer keeps no MultiMesh buffers to read back.
	if DisplayServer.get_name() == "headless":
		print("SKIP details not stacked (needs a renderer)")
	else:
		_check("details not stacked", origins.size() > total * 0.99, "%d distinct positions of %d" % [origins.size(), total])
	world.volume.queue_free()
	world.viewer.queue_free()
	await get_tree().process_frame

func _origins(features: Array) -> Array:
	var result := []
	for f in features:
		result.append(f.transform.origin.snapped(Vector3.ONE * 0.001))
	result.sort()
	return result

## Idle volume and no feature work for a while (features keep building after meshing).
func _settle(volume: JarVoxelVolume) -> void:
	var idle := 0
	var start := Time.get_ticks_msec()
	while idle < 60 and Time.get_ticks_msec() - start < 60000:
		await get_tree().process_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0
