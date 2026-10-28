extends Node
## Headless checks for population: features (persistent, on the SDF surface,
## removed by edits) and details (scattered over full-detail chunks). Run:
##   godot --headless --path demo res://tests/population_tests.tscn

var _failures := 0

func _ready() -> void:
	await _test_features()
	await _test_details()
	await _test_biomes()
	await _test_toggles()
	await _test_detail_resolution()
	await _test_detail_sphere()
	await _test_planet_orientation()
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

	# Edits nearby that leave a feature's ground alone keep it: a hole beside
	# it, a blob of earth above it (their bounding boxes do contain it).
	var kept: Array[Vector3] = []
	for f in volume.get_features_near(center + Vector3(-40, 0, 0), 30.0):
		var o: Vector3 = f.transform.origin
		if kept.all(func(k): return k.distance_to(o) > 15.0) and o.distance_to(target) > 15.0:
			kept.append(o)
		if kept.size() == 2:
			break
	volume.sphere_edit(kept[0] + Vector3(5, 0, 0), 2.0, false)
	volume.sphere_edit(kept[1] + Vector3(0, 6, 0), 2.0, true)
	await _settle(volume)
	_check("nearby edits keep features", kept.all(func(k): return not volume.get_features_near(k, 0.5).is_empty()))
	volume.regenerate_population()
	await _settle(volume)
	_check("nearby edits keep features after regenerating", kept.all(func(k): return not volume.get_features_near(k, 0.5).is_empty()))

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

## population_features_enabled / population_details_enabled: off removes
## them at once, on brings them back.
func _test_toggles() -> void:
	var detail := JarTerrainDetail.new()
	detail.mesh = BoxMesh.new()
	detail.density = 1.0
	detail.maximum_slope = 90.0
	var feature := _box_feature(0.01)
	var world := TestWorld.flat(self, Vector3(3, 30, 5), func(v: JarVoxelVolume):
		v.population_terrain_features = [feature]
		v.population_terrain_details = [detail])
	var volume: JarVoxelVolume = world.volume
	await _settle(volume)
	var before := _population_counts(volume)
	volume.population_features_enabled = false
	volume.population_details_enabled = false
	for i in 3:
		await get_tree().process_frame
	var off := _population_counts(volume)
	_check("toggles off remove population", before.x > 0 and before.y > 0 and off == Vector2i.ZERO,
		"features %d -> %d, details %d -> %d" % [before.x, off.x, before.y, off.y])
	volume.population_features_enabled = true
	volume.population_details_enabled = true
	await _settle(volume)
	var on := _population_counts(volume)
	_check("toggles on bring it back", on.x == before.x and on.y > before.y * 0.9,
		"features %d, details %d" % [on.x, on.y])
	world.volume.queue_free()
	world.viewer.queue_free()
	await get_tree().process_frame

## Details are placed from a lattice in volume space, not from the mesh's
## triangles: the same ground gets the same details at another voxel size
## (only their height follows the mesh, and on steep ground their position a
## little), at the same density.
func _test_detail_resolution() -> void:
	var detail := JarTerrainDetail.new()
	detail.mesh = BoxMesh.new()
	detail.density = 2.0
	detail.maximum_slope = 90.0
	detail.draw_distance = 20.0
	var center := Vector3(3, 0, 5)
	var sets := []
	for voxel in [1.0, 0.5]:
		var world := TestWorld.flat(self, center + Vector3(0, 12, 0), func(v: JarVoxelVolume):
			v.voxel_size = voxel
			v.population_terrain_details = [detail])
		var volume: JarVoxelVolume = world.volume
		await _settle(volume)
		var points := []
		for chunk in volume.get_children():
			if not (chunk is JarVoxelChunk and chunk.visible):
				continue
			for n in chunk.get_children():
				if n is MultiMeshInstance3D and n.visible and n.multimesh:
					# Read from the buffer: headless rendering keeps no transforms.
					# Per instance: 3 rows of (basis x, y, z, origin), then custom data.
					var buffer: PackedFloat32Array = n.multimesh.buffer
					for i in n.multimesh.instance_count:
						var k: int = i * 16
						var p: Vector3 = chunk.position + Vector3(buffer[k + 3], buffer[k + 7], buffer[k + 11])
						if Vector2(p.x - center.x, p.z - center.z).length() < 10.0:
							points.append(p)
					break # the far mesh's multimesh holds the same instances
		sets.append(points)
		world.volume.queue_free()
		world.viewer.queue_free()
		await get_tree().process_frame
	# The meshes differ a little (up to a few tenths of a meter), and so do the
	# heights of the details on them; on steep ground also their sideways place.
	var matched := 0
	for p: Vector3 in sets[0]:
		for q: Vector3 in sets[1]:
			if p.distance_to(q) < 0.3:
				matched += 1
				break
	_check("details independent of voxel size", sets[0].size() > 100 and matched > 0.9 * sets[0].size()
		and absf(sets[1].size() - sets[0].size()) < 0.1 * sets[0].size(),
		"%d and %d instances, %d at the same place" % [sets[0].size(), sets[1].size(), matched])

## Details on a smooth sphere: each triangle takes candidates from the axis
## plane it faces most, so the sphere is covered by six such regions. The
## density must be the same at a region's center, on a seam between two and at
## a corner of three; the spacing (mean distance to the nearest neighbor, 1.0
## for a perfectly random scatter of that density) shows how even it is.
func _test_detail_sphere() -> void:
	var sdf := JarTerrainSdf.new()
	sdf.shape = JarTerrainSdf.SHAPE_SPHERE
	sdf.radius = 400.0
	sdf.erosion = false
	var detail := JarTerrainDetail.new()
	detail.mesh = BoxMesh.new()
	detail.density = 2.0
	detail.maximum_slope = 90.0
	detail.draw_distance = 24.0
	var holder := Node3D.new()
	add_child(holder)
	var volume := JarVoxelVolume.new()
	volume.chunk_scene = TestWorld.CHUNK_SCENE
	volume.extent = 1024.0
	volume.sdf = sdf
	volume.population_terrain_details = [detail]
	holder.add_child(volume)
	var spots := {center = Vector3(0, 1, 0), seam = Vector3(1, 1, 0).normalized(), corner = Vector3(1, 1, 1).normalized()}
	for name in spots:
		var viewer := JarVoxelViewer.new()
		viewer.position = spots[name] * 404.0
		holder.add_child(viewer)
	await _settle(volume)
	var points: Array[Vector3] = []
	for chunk in volume.get_children():
		if not (chunk is JarVoxelChunk and chunk.visible):
			continue
		for n in chunk.get_children():
			if n is MultiMeshInstance3D and n.visible and n.multimesh:
				var buffer: PackedFloat32Array = n.multimesh.buffer
				for i in n.multimesh.instance_count:
					var k: int = i * 16
					points.append(chunk.position + Vector3(buffer[k + 3], buffer[k + 7], buffer[k + 11]))
				break
	var radius := 8.0
	var densities := {}
	for name in spots:
		var at: Vector3 = spots[name] * 400.0
		var near: Array[Vector3] = []
		for p in points:
			if p.distance_to(at) < radius + 2.0:
				near.append(p)
		var inside := 0
		var spacing := 0.0
		for p in near:
			if p.distance_to(at) >= radius:
				continue
			inside += 1
			var best := INF
			for q in near:
				if q != p:
					best = minf(best, p.distance_squared_to(q))
			spacing += sqrt(best)
		var density := inside / (PI * radius * radius)
		densities[name] = density
		# A random scatter of density d has mean nearest-neighbor distance 0.5 / sqrt(d).
		print("INFO sphere details %s: %.2f per square meter, spacing %.2f of random" % [name, density,
			(spacing / maxi(inside, 1)) / (0.5 / sqrt(maxf(density, 1e-6)))])
	var low: float = densities.values().min()
	var high: float = densities.values().max()
	_check("sphere details same density everywhere", low > 0.0 and high < low * 1.15 and absf(low - detail.density) < 0.2 * detail.density,
		"%.2f to %.2f per square meter (density %.1f)" % [low, high, detail.density])
	holder.queue_free()
	await get_tree().process_frame

## On a planet, features stand along the local up (or the surface normal with
## align_with_normal). Flat ground hides a transposed basis (rotations about
## up look the same), so this needs a sphere.
func _test_planet_orientation() -> void:
	var sdf := JarTerrainSdf.new()
	sdf.shape = JarTerrainSdf.SHAPE_SPHERE
	sdf.radius = 400.0
	var noise := FastNoiseLite.new()
	noise.frequency = 0.01
	sdf.noise = noise
	sdf.height = 20.0
	sdf.erosion = false
	var upright := _box_feature(0.01)
	upright.maximum_slope = 90.0
	var aligned := _box_feature(0.01)
	aligned.align_with_normal = true
	var holder := Node3D.new()
	add_child(holder)
	var volume := JarVoxelVolume.new()
	volume.chunk_scene = TestWorld.CHUNK_SCENE
	volume.extent = 1024.0
	volume.sdf = sdf
	volume.population_terrain_features = [upright, aligned]
	holder.add_child(volume)
	var viewer := JarVoxelViewer.new()
	var spot := Vector3(1.0, 0.4, 0.7).normalized() * 430.0
	viewer.position = spot
	holder.add_child(viewer)
	await _settle(volume)
	var worst := [0.0, 0.0]
	var counts := [0, 0]
	for f in volume.get_features_near(spot, 120.0):
		var t: Transform3D = f.transform
		var i := 1 if f.feature == aligned else 0
		var target: Vector3 = sdf.sample_normal(t.origin) if i == 1 else volume.get_up_direction_at(t.origin)
		worst[i] = maxf(worst[i], rad_to_deg(t.basis.y.angle_to(target)))
		counts[i] += 1
	_check("planet features stand along up", counts[0] > 10 and worst[0] < 1.0, "%d, worst %.2f degrees" % [counts[0], worst[0]])
	_check("planet features align with the normal", counts[1] > 10 and worst[1] < 2.0, "%d, worst %.2f degrees" % [counts[1], worst[1]])
	holder.queue_free()
	await get_tree().process_frame

## (feature instances, detail instances) currently in multimeshes.
func _population_counts(volume: JarVoxelVolume) -> Vector2i:
	var counts := Vector2i.ZERO
	for n in volume.find_children("*", "MultiMeshInstance3D", true, false):
		if n.is_queued_for_deletion() or not n.multimesh or not n.is_visible_in_tree():
			continue
		if n.get_parent() is JarVoxelChunk:
			counts.y += n.multimesh.instance_count
		else:
			counts.x += n.multimesh.instance_count
	return counts

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
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() and not volume.is_generating_details() else 0

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

