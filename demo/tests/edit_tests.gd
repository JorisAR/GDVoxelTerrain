extends Node
## Headless end-to-end checks for volume edits, verified with physics raycasts
## against the rebuilt chunk colliders. Run:
##   godot --headless --path demo res://tests/edit_tests.tscn

var _failures := 0
var volume: JarVoxelVolume

func _ready() -> void:
	var world := TestWorld.flat(self, Vector3(-77, 63, -7), func(v: JarVoxelVolume):
		if OS.get_environment("MESHER") == "dc":
			v.meshing_algorithm = JarVoxelVolume.MESHING_DUAL_CONTOURING)
	volume = world.volume
	print("Mesher: ", ["surface nets", "dual contouring"][volume.meshing_algorithm])
	_run.call_deferred(world.viewer)

func _run(camera: Node3D) -> void:
	var at := camera.global_position
	await _settle()
	var ground := _ground_height(at)
	# Bring the viewer down to the edit site so it is meshed at full detail.
	if not is_nan(ground):
		camera.global_position = Vector3(at.x + 6, ground + 8, at.z)
		volume.force_update_lod()
		await _settle()
	_check("ground found", not is_nan(ground), "y=%.2f" % ground)
	if is_nan(ground):
		return _finish()

	# 1. A box rotated 45 degrees around Y, 10 m tall, centered on the ground.
	var box := JarBoxSdf.new()
	box.extent = Vector3(3, 5, 3)
	volume.edit(box, JarVoxelVolume.SDF_OPERATION_UNION, Transform3D(Basis(Vector3.UP, PI / 4), Vector3(at.x, ground, at.z)))
	await _settle()
	var top := _ground_height(at)
	_check("box raises ground", absf(top - (ground + 5.0)) < 1.0, "top=%.2f expected=%.2f" % [top, ground + 5.0])
	# (2.5, 2.5) would be inside the unrotated box, but rotated 45 degrees it lies
	# 3.54 m out along a face normal, past the 3 m half-extent.
	var side := _ground_height(at + Vector3(2.5, 0, 2.5))
	_check("box is rotated", side < top - 2.0, "side=%.2f" % side)
	# 2.6 m in along a face normal, 0.4 m from the box edge: a sharp mesher keeps
	# this at full height (informational: how far the edge is rounded off).
	var near_edge := _ground_height(at + Vector3(2.6, 0, 0).rotated(Vector3.UP, PI / 4))
	print("INFO box edge height %.2f of %.2f" % [near_edge, top])

	# 2. Carve a sphere into the box top.
	volume.sphere_edit(Vector3(at.x, top, at.z), 2.0, false)
	await _settle()
	var pit := _ground_height(at)
	_check("sphere carves", pit < top - 1.0, "pit=%.2f" % pit)

	# 3. Stamp a prism mesh, scaled 2x, beside the box.
	var prism := PrismMesh.new()
	prism.size = Vector3(2, 2, 2)
	var mesh_sdf := JarMeshSdf.new()
	mesh_sdf.mesh = prism
	var spot := at + Vector3(12, 0, 0)
	var spot_ground := _ground_height(spot)
	volume.edit(mesh_sdf, JarVoxelVolume.SDF_OPERATION_UNION,
		Transform3D(Basis().scaled(Vector3.ONE * 2.0), Vector3(spot.x, spot_ground + 2.0, spot.z)))
	await _settle()
	var peak := _ground_height(spot)
	# Surface nets rounds the apex; mesher_tests checks it exactly under dual contouring.
	_check("mesh stamp", peak > spot_ground + 2.5 and peak < spot_ground + 4.2, "peak=%.2f apex=%.2f" % [peak, spot_ground + 4.0])

	# 4. Paint material 5 onto flat ground: the shape stays, the weight shows up
	# in the vertex data (materials 4-7 live in CUSTOM0).
	var paint_spot := at + Vector3(-12, 0, 0)
	var paint_ground := _ground_height(paint_spot)
	var brush := JarSphereSdf.new()
	brush.radius = 3.0
	volume.paint(brush, Transform3D(Basis(), Vector3(paint_spot.x, paint_ground, paint_spot.z)), 5)
	await _settle()
	_check("paint keeps shape", absf(_ground_height(paint_spot) - paint_ground) < 0.05,
		"%.3f -> %.3f" % [paint_ground, _ground_height(paint_spot)])
	var center := Vector3(paint_spot.x, paint_ground, paint_spot.z)
	var away := Vector3(paint_spot.x + 8, _ground_height(paint_spot + Vector3(8, 0, 0)), paint_spot.z)
	# Every material mode encodes material 5 differently; decode each.
	for mode in [JarVoxelVolume.MATERIAL_MODE_WEIGHTS, JarVoxelVolume.MATERIAL_MODE_SPLIT, JarVoxelVolume.MATERIAL_MODE_INDEXED]:
		volume.material_mode = mode
		await _settle()
		var label: String = ["weights", "split", "indexed"][mode]
		var weights := _material_weights_near(center, 1.5, mode)
		_check("paint sets material (%s)" % label, weights.size() > 0 and weights.min() > 0.9, "material 5 weight near center: %s" % [weights.slice(0, 4)])
		var outside := _material_weights_near(away, 1.5, mode)
		_check("paint stays inside (%s)" % label, outside.size() > 0 and outside.max() < 0.01, "%s" % [outside.slice(0, 4)])
	volume.material_mode = JarVoxelVolume.MATERIAL_MODE_WEIGHTS
	await _settle()

	# 5. A small edit on an idle volume is applied, meshed and collidable by
	# the next frame (the volume waits a few ms for it instead of handing it
	# along over frames; it took 9 frames when debris searched with normals).
	var dig := at + Vector3(0, 0, 12)
	var dig_ground := _ground_height(dig)
	volume.sphere_edit(Vector3(dig.x, dig_ground, dig.z), 1.5, false)
	# In physics ticks: headless frames run uncapped, colliders change on ticks.
	var ticks := 0
	while ticks < 60 and _ground_height(dig) > dig_ground - 0.5:
		await get_tree().physics_frame
		ticks += 1
	_check("small edit shows next frame", ticks <= 1, "%d physics ticks" % ticks)
	await _settle()

	# 6. Unbounded SDFs are rejected by edit() instead of editing the whole world.
	volume.edit(JarPlaneSdf.new(), JarVoxelVolume.SDF_OPERATION_UNION, Transform3D())
	_check("unbounded edit rejected", not volume.is_building())
	await _cleanup()
	await _skip_outside()
	await _large_edits()
	await _climate_plot()
	await _commands()
	await _find_land()
	await _save_and_load()
	await _digs_while_moving()
	_finish()

## Flies `viewer` away in steps and waits at the end.
func _fly_to(volume: JarVoxelVolume, viewer: Node3D, target: Vector3) -> void:
	var start: Vector3 = volume.to_local(viewer.global_position)
	for step in range(1, 5):
		viewer.global_position = volume.to_global(start.lerp(target, step / 4.0))
		await _settle_on(volume)

func _settle_on(target: JarVoxelVolume) -> void:
	var idle := 0
	var start := Time.get_ticks_msec()
	while idle < 30 and Time.get_ticks_msec() - start < 60000:
		await get_tree().physics_frame
		idle = idle + 1 if not target.is_building() and not target.is_meshing() else 0

## Edited detail is kept when the viewer leaves, and only that: the same flight
## with one small dig ends with about the same octree (stopping at edited nodes
## kept 2.4 million more for it), and everything edited is there on the way
## back, also where a later edit split an edited leaf.
func _cleanup() -> void:
	var counts := []
	for dig in [false, true]:
		var holder := Node.new()
		add_child(holder)
		var world := TestWorld.flat(holder, Vector3(0, 40, 0))
		var v: JarVoxelVolume = world.volume
		await _settle_on(v)
		var ground: float = v.raycast(Vector3(0, 300, 0), Vector3(0, -300, 0)).position.y
		if dig:
			v.sphere_edit(Vector3(0, ground, 0), 1.5, false)
			await _settle_on(v)
		await _fly_to(v, world.viewer, Vector3(4800, 40, 0))
		counts.append(v.get_node_count())
		holder.queue_free()
		await get_tree().process_frame
	_check("cleanup: a small dig keeps little", counts[1] - counts[0] < 20000 and counts[0] > 0,
		"%d nodes without it, %d with" % counts)

	var holder := Node.new()
	add_child(holder)
	var world := TestWorld.flat(holder, Vector3(0, 40, 0))
	var v: JarVoxelVolume = world.volume
	await _settle_on(v)
	var ground: float = v.raycast(Vector3(0, 300, 0), Vector3(0, -300, 0)).position.y
	var pit := Vector3(0, ground, 0)
	v.sphere_edit(pit, 24.0, false) # deep inside it, large air leaves
	await _settle_on(v)
	var side := pit + Vector3(14, -6, 0)
	# From afar, where a leaf's band of samples is wide: the edit splits air
	# leaves, whose children copy an edited value.
	await _fly_to(v, world.viewer, Vector3(1500, 40, 0))
	var fill := pit + Vector3(-12, -16, 0)
	v.sphere_edit(fill, 3.0, true)
	await _settle_on(v)
	await _fly_to(v, world.viewer, Vector3(4800, 40, 0))
	await _fly_to(v, world.viewer, Vector3(0, 40, 0))
	var refilled := 0
	var inside := 0
	for x in [-1.0, 0.0, 1.0]:
		for y in [0.0, 1.0]:
			for z in [-1.0, 0.0, 1.0]:
				var offset := Vector3(x, y, z)
				if offset == Vector3.ZERO:
					continue
				var point := fill + offset.normalized() * 6.0
				if point.distance_to(pit) < 23.0: # inside the pit
					inside += 1
					refilled += 1 if v.get_distance(point) <= 0.0 else 0
	_check("cleanup: air around a later edit stays air", refilled == 0 and inside > 8, "%d of %d points solid again" % [refilled, inside])
	_check("cleanup: the pit is still dug", v.get_distance(side) > 2.0, "%.2f m from ground" % v.get_distance(side))
	var bottom := v.raycast(pit + Vector3(14, 30, 0), pit + Vector3(14, -40, 0))
	_check("cleanup: its floor is where it was", not bottom.is_empty() and bottom.position.y < ground - 12.0,
		"%s, ground %.2f" % [bottom.get("position"), ground])
	var added := v.get_distance(pit + Vector3(-12, -16, 0))
	_check("cleanup: what was added inside it stays", added < 0.0, "%.2f" % added)
	holder.queue_free()
	await get_tree().process_frame

## Edits skip what the shape cannot reach when its distances can be trusted:
## the same edits through a wrapper that makes them walk everything (it
## changes no distance) give the same surface, with far fewer nodes visited.
func _skip_outside() -> void:
	var heights := []
	var visits := []
	for trusted in [true, false]:
		var holder := Node.new()
		add_child(holder)
		var world := TestWorld.flat(holder, Vector3(0, 40, 0))
		var v: JarVoxelVolume = world.volume
		await _settle_on(v)
		var ground: float = v.raycast(Vector3(0, 300, 0), Vector3(0, -300, 0)).position.y
		var visited := 0
		var edits := [[Vector3(0, ground, 0), 7.0, false], [Vector3(10, ground + 2, 4), 5.0, true],
			[Vector3(-6, ground - 2, -8), 9.0, false]]
		for e in edits:
			var sphere := JarSphereSdf.new()
			sphere.radius = e[1]
			var shape: JarSignedDistanceField = sphere
			if not trusted:
				shape = JarDisplaceSdf.new() # no noise: the same distances
				shape.sdf = sphere
			v.edit(shape, JarVoxelVolume.SDF_OPERATION_UNION if e[2] else JarVoxelVolume.SDF_OPERATION_SUBTRACTION,
				Transform3D(Basis(), e[0]))
			var idle := 0
			while idle < 30:
				await get_tree().process_frame
				visited += JarVoxelStats.get_frame().counts["edit nodes"]
				idle = idle + 1 if not v.is_building() and not v.is_meshing() else 0
		var sampled := PackedFloat32Array()
		for x in range(-24, 25, 4):
			for z in range(-24, 25, 4):
				var hit := v.raycast(Vector3(x + 0.3, 200, z + 0.3), Vector3(x + 0.3, -200, z + 0.3))
				sampled.append(hit.position.y if hit else INF)
		heights.append(sampled)
		visits.append(visited)
		holder.queue_free()
		await get_tree().process_frame
	var worst := 0.0
	for i in heights[0].size():
		worst = maxf(worst, absf(heights[0][i] - heights[1][i]))
	_check("skipping changes no surface", worst < 0.005, "largest difference %.4f m" % worst)
	_check("skipping visits fewer nodes", visits[0] > 0 and visits[0] < 0.7 * visits[1], "%d instead of %d" % visits)

## Large edits run on the pool: the shape, the mesh and the materials must
## come out as from a single thread. No frame waits for them.
func _large_edits() -> void:
	var holder := Node.new()
	add_child(holder)
	var world := TestWorld.flat(holder, Vector3(0, 40, 0))
	var v: JarVoxelVolume = world.volume
	await _settle_on(v)
	var ground: float = v.raycast(Vector3(0, 300, 0), Vector3(0, -300, 0)).position.y
	var pit := Vector3(0, ground, 0)
	v.sphere_edit(pit, 28.0, false, 5)
	await _settle_on(v)
	var bottom := v.raycast(pit + Vector3.UP * 50.0, pit + Vector3.DOWN * 50.0)
	_check("large dig", not bottom.is_empty() and absf(bottom.position.y - (ground - 28.0)) < 0.3,
		"floor %s, expected %.2f" % [bottom.get("position"), ground - 28.0])
	var rim := pit + Vector3(28, 0, 0)
	var r := Topology.analyze(v, v.project_to_surface(rim).position, 30.0)
	_check("large dig: mesh closed", r.open == 0 and r.triangles > 1000, "%d open edges in %d triangles" % [r.open, r.triangles])

	v.sphere_edit(pit + Vector3(14, -14, 0), 20.0, true)
	await _settle_on(v)
	_check("large add over it", v.get_distance(pit + Vector3(14, -14, 0)) < -15.0 and v.get_distance(pit + Vector3(-18, -8, 0)) > 3.0,
		"%.2f inside, %.2f across the pit" % [v.get_distance(pit + Vector3(14, -14, 0)), v.get_distance(pit + Vector3(-18, -8, 0))])
	r = Topology.analyze(v, v.project_to_surface(pit + Vector3(-5, -20, 0)).position, 30.0)
	_check("large add: mesh closed", r.open == 0 and r.triangles > 1000, "%d open edges in %d triangles" % [r.open, r.triangles])

	var brush := JarSphereSdf.new()
	brush.radius = 24.0
	var spot := pit + Vector3(70, 0, 0)
	var before := _ground_height(spot)
	volume = v
	v.paint(brush, Transform3D(Basis(), Vector3(spot.x, before, spot.z)), 5)
	await _settle_on(v)
	var weights := _material_weights_near(Vector3(spot.x, before, spot.z), 6.0)
	_check("large paint", weights.size() > 0 and weights.min() > 0.9 and absf(_ground_height(spot) - before) < 0.05,
		"weights %s" % [weights.slice(0, 3)])
	holder.queue_free()
	await get_tree().process_frame

## What edits changed survives a save: a new volume loading it has the same
## ground, and features stay gone; damaged bytes change nothing.
## Commands from one volume played on another, elsewhere in the scene, give the
## same voxels; bad commands are refused.
func _commands() -> void:
	# Detail follows every viewer: others would stand elsewhere for the second volume.
	var others := get_tree().root.find_children("*", "JarVoxelViewer", true, false)
	for viewer: JarVoxelViewer in others:
		viewer.enabled = false
	var holder := Node3D.new()
	add_child(holder)
	var first := TestWorld.flat(holder, Vector3(0, 30, 0))
	var a: JarVoxelVolume = first.volume
	await _settle_on(a)
	var commands: Array[Dictionary] = []
	a.edit_made.connect(func(command: Dictionary): commands.append(command))
	var ground: float = a.raycast(Vector3(0, 300, 0), Vector3(0, -300, 0)).position.y
	var pit := Vector3(0, ground, 0)
	a.sphere_edit(pit, 7.0, false, 2)
	var box := JarBoxSdf.new()
	var shaped := JarTransformSdf.new()
	shaped.sdf = box
	a.edit(shaped, JarVoxelVolume.SDF_OPERATION_SMOOTH_UNION, Transform3D(Basis(Vector3.UP, 0.6), pit + Vector3(16, 0, 3)), 3, 1.5)
	var brush := JarSphereSdf.new()
	brush.radius = 5.0
	a.paint(brush, Transform3D(Basis(), pit + Vector3(-14, 0, 0)), 4)
	await _settle_on(a)
	_check("commands: made", commands.size() == 3, str(commands.size()))
	# Over a network: plain data, and no objects in it.
	var sent: Array = bytes_to_var(var_to_bytes(commands))
	var saved := a.save_edits()
	# Alone: a volume's detail follows every viewer, and this is to be the same.
	a.queue_free()
	first.viewer.queue_free()
	await get_tree().process_frame
	var far := Node3D.new()
	far.position = Vector3(300, 40, -200)
	holder.add_child(far)
	var second := TestWorld.flat(far, Vector3(0, 30, 0))
	var b: JarVoxelVolume = second.volume
	await _settle_on(b)
	var echoed := []
	b.edit_made.connect(func(command: Dictionary): echoed.append(command))
	for command: Dictionary in sent:
		_check("commands: applied", b.apply_edit_command(command) == OK)
	await _settle_on(b)
	_check("commands: not sent on", echoed.is_empty())
	var played := b.save_edits()
	_check("commands: same voxels", saved == played, "%d, %d bytes" % [saved.size(), played.size()])
	var hit: Dictionary = b.raycast(far.to_global(pit + Vector3.UP * 40.0), far.to_global(pit + Vector3.DOWN * 40.0))
	_check("commands: same ground", not hit.is_empty() and absf(far.to_local(hit.position).y - (ground - 7.0)) < 0.3, "%s" % hit.get("position"))
	var bad: Array[Dictionary] = [{}, {"sdf": {"class": "OS"}, "transform": Transform3D(), "operation": 0},
		{"sdf": {"class": "JarSphereSdf"}, "transform": 5, "operation": 0},
		{"sdf": {"path": "user://x.tres"}, "transform": Transform3D(), "operation": 0},
		{"sdf": {"class": "JarSphereSdf"}, "transform": Transform3D(), "operation": 99}]
	for command in bad:
		_check("commands: refused", b.apply_edit_command(command) != OK, str(command))
	holder.queue_free()
	for viewer: JarVoxelViewer in others:
		viewer.enabled = true
	await get_tree().process_frame

## The climate plot moves biomes, and its picture agrees with the map.
func _climate_plot() -> void:
	var cold := JarBiome.new()
	cold.temperature = -0.5
	var warm := JarBiome.new()
	warm.temperature = 0.5
	warm.moisture = 0.5
	var map := JarClimateBiomeMap.new()
	map.biomes = [cold, warm]
	var plot: Control = load("res://addons/jar_voxel_terrain/climate_plot.gd").new()
	add_child(plot)
	plot.size = Vector2(260, 284)
	plot.set_map(map)
	_check("plot: finds a dot", plot.biome_at(plot.to_point(Vector2(0.5, 0.5))) == 1)
	_check("plot: nothing elsewhere", plot.biome_at(plot.to_point(Vector2(-0.9, 0.9))) == -1)
	plot.move_biome(0, plot.to_point(Vector2(0.2, -0.4)))
	_check("plot: moves", is_equal_approx(cold.temperature, 0.2) and is_equal_approx(cold.moisture, -0.4), "%f, %f" % [cold.temperature, cold.moisture])
	plot.move_biome(1, Vector2(9000, -9000))
	_check("plot: stays in range", is_equal_approx(warm.temperature, 1.0) and is_equal_approx(warm.moisture, 1.0))
	plot.queue_free()
	await get_tree().process_frame

## find_land gives dry, gentle ground on a JarTerrainSdf.
func _find_land() -> void:
	var planet: JarTerrainSdf = load("res://demo/planet/planet_terrain.tres")
	var holder := Node.new()
	add_child(holder)
	var world := TestWorld.flat(holder, Vector3(0, 30, 0), func(v: JarVoxelVolume):
		v.sdf = planet
		v.extent = 40000.0)
	var v: JarVoxelVolume = world.volume
	for direction: Vector3 in [Vector3(1, 0.2, 0), Vector3(-0.4, 0.9, 0.2), Vector3(0.3, -0.7, 0.6), Vector3(-1, -0.3, -0.6)]:
		var from := direction.normalized() * (planet.radius + 200.0)
		var land := v.find_land(from)
		var height := land.length() - planet.radius - planet.sea_level
		_check("land found", height >= 3.9 and height <= 300.1, "%.1f m up, %.0f m from the start" % [height, land.distance_to(from)])
	holder.queue_free()
	await get_tree().process_frame

func _save_and_load() -> void:
	var holder := Node.new()
	add_child(holder)
	var feature := JarTerrainFeature.new()
	feature.mesh = BoxMesh.new()
	feature.density = 0.02
	feature.draw_distance = 150.0
	feature.maximum_slope = 90.0
	var world := TestWorld.flat(holder, Vector3(0, 30, 0), func(v: JarVoxelVolume):
		TestWorld.populate(v, [feature]))
	var v: JarVoxelVolume = world.volume
	await _settle_on(v)
	await _settle_features(v)
	var ground: float = v.raycast(Vector3(0, 300, 0), Vector3(0, -300, 0)).position.y
	var pit := Vector3(0, ground, 0)
	v.sphere_edit(pit, 12.0, false, 5)
	var spot := pit + Vector3(30, 0, 0)
	var brush := JarSphereSdf.new()
	brush.radius = 6.0
	spot = v.raycast(spot + Vector3.UP * 60.0, spot + Vector3.DOWN * 60.0).position
	v.paint(brush, Transform3D(Basis(), spot), 5)
	await _settle_on(v)
	await _settle_features(v)
	volume = v
	volume = v
	var painted := _material_weights_near(spot, 3.0)
	_check("paint before saving", painted.size() > 0 and painted.max() > 0.9, "weights %s" % [painted.slice(0, 3)])
	var population: JarVoxelPopulation = TestWorld.population(v)
	var chopped: Vector3 = population.get_features_near(pit + Vector3(50, 0, 30), 40.0)[0].transform.origin
	population.remove_features(chopped, 0.5)
	var before := _origins_of(population, Vector3.ZERO, 120.0)
	var floor_before: Vector3 = v.raycast(pit + Vector3.UP * 40.0, pit + Vector3.DOWN * 40.0).position
	var bytes := v.save_edits()
	_check("save: small", bytes.size() > 100 and bytes.size() < 2000000, "%d bytes" % bytes.size())
	holder.queue_free()
	await get_tree().process_frame

	holder = Node.new()
	add_child(holder)
	var again := TestWorld.flat(holder, Vector3(0, 30, 0), func(n: JarVoxelVolume):
		TestWorld.populate(n, [feature])
		_check("load before the tree", n.load_edits(bytes) == OK))
	var loaded: JarVoxelVolume = again.volume
	await _settle_on(loaded)
	await _settle_features(loaded)
	var floor_after: Vector3 = loaded.raycast(pit + Vector3.UP * 40.0, pit + Vector3.DOWN * 40.0).position
	_check("load: same ground", floor_after.distance_to(floor_before) < 0.05, "%s, was %s" % [floor_after, floor_before])
	volume = loaded
	var weights := _material_weights_near(spot, 3.0)
	_check("load: paint", weights.size() > 0 and weights.max() > 0.9, "weights %s" % [weights.slice(0, 3)])
	var r := Topology.analyze(loaded, loaded.project_to_surface(pit + Vector3(12, 0, 0)).position, 20.0)
	_check("load: mesh closed", r.open == 0 and r.triangles > 200, "%d open edges in %d triangles" % [r.open, r.triangles])
	var after := _origins_of(TestWorld.population(loaded), Vector3.ZERO, 120.0)
	var regrown := after.filter(func(p): return not before.has(p)).size()
	_check("load: features stay gone", regrown <= 1, "%d back of %d, %d before" % [regrown, after.size(), before.size()])
	_check("load: chopped stays gone", TestWorld.population(loaded).get_features_near(chopped, 0.5).is_empty())

	# Over an edited volume: it starts over from the save.
	loaded.sphere_edit(pit + Vector3(-40, 0, 0), 10.0, false, 2)
	await _settle_on(loaded)
	_check("load again", loaded.load_edits(bytes) == OK)
	await _settle_on(loaded)
	var open: Vector3 = pit + Vector3(-40, 0, 0)
	_check("load: later edits gone", absf(loaded.raycast(open + Vector3.UP * 40.0, open + Vector3.DOWN * 40.0).position.y - ground) < 6.0)
	_check("save: same size again", absi(loaded.save_edits().size() - bytes.size()) < bytes.size() / 2 + 200,
		"%d, was %d" % [loaded.save_edits().size(), bytes.size()])

	var damaged := bytes.duplicate()
	damaged.resize(bytes.size() / 2)
	_check("load: damaged refused", loaded.load_edits(damaged) != OK and loaded.load_edits(PackedByteArray([1, 2, 3])) != OK)
	var other := TestWorld.flat(holder, Vector3(0, 30, 0), func(n: JarVoxelVolume): n.voxel_size = 0.5)
	_check("load: other voxel size refused", other.volume.load_edits(bytes) != OK)
	holder.queue_free()
	await get_tree().process_frame

func _origins_of(population: JarVoxelPopulation, at: Vector3, radius: float) -> Array:
	var result := []
	for f in population.get_features_near(at, radius):
		result.append(f.transform.origin.snapped(Vector3.ONE * 0.01))
	return result

## Features keep arriving after meshing is done.
func _settle_features(v: JarVoxelVolume) -> void:
	var population: JarVoxelPopulation = TestWorld.population(v)
	var last := -1
	var still := 0
	var start := Time.get_ticks_msec()
	while still < 30 and Time.get_ticks_msec() - start < 20000:
		await get_tree().process_frame
		var count: int = population.get_features_near(Vector3.ZERO, 200.0).size()
		still = still + 1 if count == last and count > 0 else 0
		last = count

## Digs ahead of a moving viewer, so edits land in LOD updates still being
## meshed: their whole group goes first, ahead of the queue it was in.
func _digs_while_moving() -> void:
	var holder := Node.new()
	add_child(holder)
	var world := TestWorld.flat(holder, Vector3(0, 30, 0))
	var v: JarVoxelVolume = world.volume
	var viewer: Node3D = world.viewer
	await _settle_on(v)
	var digs := 0
	for frame in 900:
		viewer.global_position.x += 1.5
		if frame % 12 == 0:
			var hit := v.raycast(viewer.global_position + Vector3(12, 40, 0), viewer.global_position + Vector3(12, -200, 0))
			if not hit.is_empty():
				v.sphere_edit(hit.position, 2.5, false)
				digs += 1
		await get_tree().process_frame
	await _settle_on(v)
	var spot := viewer.global_position
	_check("digs while moving", digs > 30 and not v.raycast(spot + Vector3(0, 40, 0), spot + Vector3(0, -200, 0)).is_empty(), "%d digs" % digs)
	holder.queue_free()
	await get_tree().process_frame

func _finish() -> void:
	print("Edit tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

func _check(name: String, ok: bool, detail := "") -> void:
	if not ok:
		_failures += 1
	print("%s %s %s" % ["PASS" if ok else "FAIL", name, detail])

## Waits until the volume is idle and colliders had time to upload.
func _settle() -> void:
	var idle := 0
	while idle < 30:
		await get_tree().physics_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0

func _ground_height(at: Vector3) -> float:
	var space := volume.get_world_3d().direct_space_state
	var query := PhysicsRayQueryParameters3D.create(Vector3(at.x, 500, at.z), Vector3(at.x, -500, at.z))
	var hit := space.intersect_ray(query)
	return hit.position.y if hit else NAN

## Weight of material 5 at chunk vertices within `radius` of `at`, decoded per
## material mode (weights: CUSTOM0.g; split: CUSTOM0.r is the triangle's id;
## indexed: CUSTOM0 holds 4 ids, COLOR their weights).
func _material_weights_near(at: Vector3, radius: float, mode := 0) -> Array:
	var result := []
	for c in volume.get_children():
		if not (c is JarVoxelChunk and c.visible and c.get_lod() == 0):
			continue
		var mesh: Mesh = c.get_mesh()
		if mesh.get_surface_count() == 0:
			continue
		var arrays := mesh.surface_get_arrays(0)
		var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var colors: PackedColorArray = arrays[Mesh.ARRAY_COLOR]
		var custom: PackedByteArray = arrays[Mesh.ARRAY_CUSTOM0]
		for i in verts.size():
			if (c.position + verts[i]).distance_to(at) >= radius:
				continue
			match mode:
				0:
					result.append(custom[4 * i + 1] / 255.0)
				1:
					result.append(1.0 if custom[4 * i] == 5 else 0.0)
				2:
					var w := 0.0
					var col := colors[i]
					var ws := [col.r, col.g, col.b, col.a]
					for k in 4:
						if custom[4 * i + k] == 5:
							w = ws[k]
							break
					result.append(w)
	return result
