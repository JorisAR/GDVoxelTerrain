extends Node
## Headless checks on the giant (600 km) demo planet, where 32-bit floats are
## coarse (6 cm at its radius): physics near the player must be as smooth as
## on a small world. Run:
##   godot --headless --path demo res://tests/large_world_tests.tscn

var SCENE := OS.get_environment("SCENE") if OS.get_environment("SCENE") != "" else "res://demo/giant_planet.tscn"

var _failures := 0

func _ready() -> void:
	_test_surface()
	await _test_physics()
	print("Large world tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

func _check(name: String, ok: bool, detail := "") -> void:
	if not ok:
		_failures += 1
	print("%s %s %s" % ["PASS" if ok else "FAIL", name, detail])

## The giant planet's surface is as smooth as a small one's: heights along
## lines in steps of 6.25 cm (exact at its radius, so the inputs carry no
## rounding). Floats there rounded every noise input by up to 3 cm: random
## bumps of up to 18 cm between neighboring points; real curvature (steep
## ridges and gullies) stays well under a centimeter.
func _test_surface() -> void:
	var sdf: JarTerrainSdf = load("res://demo/giant/giant_terrain.tres")
	var rng := RandomNumberGenerator.new()
	rng.seed = 5
	var worst := 0.0
	for line in 40:
		var dir := Vector3(rng.randf_range(-1, 1), rng.randf_range(-1, 1), rng.randf_range(-1, 1)).normalized()
		var base := (dir * sdf.radius * 16.0).round() / 16.0
		var heights: Array[float] = []
		for k in 64:
			heights.append(sdf.sample_height(base + Vector3(k * 0.0625, 0, 0)))
		for k in range(1, 63):
			worst = maxf(worst, absf(heights[k + 1] - 2.0 * heights[k] + heights[k - 1]))
	_check("surface smooth at 600 km", worst < 0.02, "largest second difference over 6.25 cm: %.5f m" % worst)

## A direction from the player's start where the planet is dry land.
func _land(sdf: JarTerrainSdf, start: Vector3) -> Vector3:
	var rng := RandomNumberGenerator.new()
	rng.seed = 3
	for i in 2000:
		var dir := (start + Vector3(rng.randf_range(-1, 1), rng.randf_range(-1, 1), rng.randf_range(-1, 1)) * 0.05 * (1 + i / 100)).normalized()
		if sdf.sample_height(dir * sdf.radius) > 30.0:
			return dir
	return start

## The player standing and walking, and a plain RigidBody3D dropped next to it.
func _test_physics() -> void:
	var scene: Node = load(SCENE).instantiate()
	var player: DemoPlayer = scene.get_node("Player")
	var volume: JarVoxelVolume = scene.get_node("%Planet/JarVoxelVolume")
	var sdf: JarTerrainSdf = volume.sdf
	var dir := _land(sdf, player.position.normalized())
	player.position = dir * (sdf.radius + sdf.sample_height(dir * sdf.radius) + 3.0)
	add_child(scene)
	await _wait_until(func(): return player._spawned and player.is_on_floor(), 60000)
	_check("spawns on ground", player.is_on_floor())
	# The floating origin moved the scene's contents, not its root: through a
	# moved root every position was composed from two far ones (1/32 m steps).
	_check("scene root stays at the origin", scene.transform == Transform3D(), "%s" % scene.transform)
	for i in 60:
		await get_tree().physics_frame

	# Standing still: not a millimeter of motion (the ground does not move).
	var last := player.global_position
	var worst := 0.0
	var start := last
	for i in 120:
		await get_tree().physics_frame
		worst = maxf(worst, player.global_position.distance_to(last))
		last = player.global_position
	_check("standing player stays put", worst < 0.001 and last.distance_to(start) < 0.002,
		"largest step %.4f m, drift %.4f m" % [worst, last.distance_to(start)])

	# Walking: the velocity changes smoothly from tick to tick (rounded
	# positions show up as spikes of several m/s).
	Input.action_press("move_forward")
	var positions: Array[Vector3] = []
	for i in 150:
		await get_tree().physics_frame
		positions.append(player.global_position)
	Input.action_release("move_forward")
	var jerk := 0.0
	var speed := 0.0
	for i in range(40, positions.size() - 1):
		var v0 := (positions[i] - positions[i - 1]) * 60.0
		var v1 := (positions[i + 1] - positions[i]) * 60.0
		jerk = maxf(jerk, v1.distance_to(v0))
		speed = maxf(speed, v1.length())
	_check("walking is smooth", speed > 3.0 and jerk < 1.0, "speed %.2f m/s, largest change %.3f m/s per tick" % [speed, jerk])

	# A plain RigidBody3D, no setup: falls towards the planet and rests where dropped.
	await _wait_until(func(): return player.velocity.length() < 0.1, 3000)
	var up := player.up_direction
	var side := up.cross(Vector3(0.3, 0.1, 0.9)).normalized()
	var body := RigidBody3D.new()
	var shape := CollisionShape3D.new()
	shape.shape = BoxShape3D.new()
	body.add_child(shape)
	add_child(body)
	var drop := player.global_position + up * 5.0 + side * 3.0
	body.global_position = drop
	await get_tree().create_timer(4.0).timeout
	# Under the body itself: on a slope it may have slid a little.
	var query := PhysicsRayQueryParameters3D.create(body.global_position + up * 10.0, body.global_position - up * 30.0)
	query.exclude = [body.get_rid(), player.get_rid()]
	var hit := volume.get_world_3d().direct_space_state.intersect_ray(query)
	var above: float = (body.global_position - hit.position).dot(up) if hit else NAN
	var sideways := (body.global_position - drop).slide(up).length()
	_check("rigid body falls and rests", absf(above - 0.5) < 0.6 and sideways < 5.0 and body.linear_velocity.length() < 1.0,
		"%.2f m above the ground, %.2f m sideways, %.2f m/s" % [above, sideways, body.linear_velocity.length()])
	scene.queue_free()
	body.queue_free()
	await get_tree().process_frame

func _wait_until(condition: Callable, timeout_ms: int) -> void:
	var start := Time.get_ticks_msec()
	while not condition.call() and Time.get_ticks_msec() - start < timeout_ms:
		await get_tree().physics_frame
