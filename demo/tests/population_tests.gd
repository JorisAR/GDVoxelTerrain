extends Node
## Headless checks for population: features (persistent, on the SDF surface,
## removed by edits) and details (scattered over full-detail chunks). Run:
##   godot --headless --path demo res://tests/population_tests.tscn

var _failures := 0

func _ready() -> void:
	await _test_features()
	await _test_details()
	await _test_biomes()
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

	# Layers: a second feature placed after the first keeps clear of its
	# footprint. Cells are generated independently, so pairs across a cell
	# border can still overlap: allow a small share.
	feature.mask_noise = null
	feature.footprint = 6.0
	var small := _box_feature(0.02)
	small.footprint = 2.0
	volume.population_terrain_features = [feature, small]
	await _settle(volume)
	var big_ones := []
	var small_ones := []
	for f in volume.get_features_near(center, 100.0):
		(big_ones if f.feature == feature else small_ones).append(f.transform.origin)
	var too_close := 0
	for a in small_ones:
		for b in big_ones:
			if a.distance_to(b) < 6.0:
				too_close += 1
	_check("footprints keep layers apart", small_ones.size() > 0 and too_close <= small_ones.size() * 0.15,
		"%d of %d small features inside a big footprint" % [too_close, small_ones.size()])

	# Clusters: the same density, gathered into groves, is much lumpier
	# (index of dispersion of counts in 20 m bins).
	volume.population_terrain_features = [feature]
	feature.footprint = 2.0
	await _settle(volume)
	var even := _dispersion(volume.get_features_near(center, 100.0), center)
	feature.cluster_spacing = 60.0
	feature.cluster_radius = 12.0
	volume.regenerate_population()
	await _settle(volume)
	var clustered := _dispersion(volume.get_features_near(center, 100.0), center)
	_check("clusters", clustered > even * 2.0, "dispersion %.2f clustered vs %.2f even" % [clustered, even])

	world.volume.queue_free()
	world.viewer.queue_free()
	await get_tree().process_frame

## Variance / mean of feature counts in 20 m bins: about 1 for random
## placement, below for even spacing, far above for clusters.
func _dispersion(features: Array, center: Vector3) -> float:
	var bins := {}
	for x in range(-3, 3):
		for z in range(-3, 3):
			bins[Vector2i(x, z)] = 0
	for f in features:
		var o: Vector3 = f.transform.origin - center
		var k := Vector2i(floori(o.x / 20.0), floori(o.z / 20.0))
		if bins.has(k):
			bins[k] += 1
	var mean := 0.0
	for k in bins:
		mean += bins[k]
	mean /= bins.size()
	var variance := 0.0
	for k in bins:
		variance += (bins[k] - mean) * (bins[k] - mean)
	variance /= bins.size()
	return variance / maxf(mean, 1e-3)

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

## Two biomes (cold / warm), each with its own feature: weights sum to 1, and
## each feature only grows where its biome is.
func _test_biomes() -> void:
	var temperature := FastNoiseLite.new()
	temperature.frequency = 0.01
	var cold := JarBiome.new()
	cold.temperature = -0.6
	var warm := JarBiome.new()
	warm.temperature = 0.6
	var cold_feature := _box_feature(0.01)
	var warm_feature := _box_feature(0.01)
	cold.features = [cold_feature]
	warm.features = [warm_feature]
	var map := JarBiomeMap.new()
	map.temperature_noise = temperature
	map.biomes = [cold, warm]
	var world := TestWorld.flat(self, Vector3(3, 30, 5), func(v: JarVoxelVolume):
		v.population_biome_map = map)
	var volume: JarVoxelVolume = world.volume
	await _settle(volume)
	var center := Vector3(3, 0, 5)
	var sums_ok := true
	for i in 50:
		var w: Dictionary = volume.get_biome_weights_at(center + Vector3(i * 3.1, 0, i * 1.7))
		var total := 0.0
		for b in w:
			total += w[b]
		sums_ok = sums_ok and absf(total - 1.0) < 0.01
	_check("biome weights sum to 1", sums_ok)
	var counts := {cold_feature: 0, warm_feature: 0}
	var misplaced := 0
	for f in volume.get_features_near(center, 150.0):
		counts[f.feature] += 1
		var biome: JarBiome = cold if f.feature == cold_feature else warm
		var w: Dictionary = volume.get_biome_weights_at(f.transform.origin)
		if w.get(biome, 0.0) < 0.02:
			misplaced += 1
	_check("each biome grows its features", counts[cold_feature] > 10 and counts[warm_feature] > 10,
		"cold %d, warm %d" % [counts[cold_feature], counts[warm_feature]])
	_check("features stay in their biome", misplaced == 0, "%d outside their biome" % misplaced)
	world.volume.queue_free()
	world.viewer.queue_free()
	await get_tree().process_frame

