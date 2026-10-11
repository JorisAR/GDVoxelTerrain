extends Node
## Headless checks for the demo player on a flat world and on a planet, driven
## through the real input actions. Run:
##   godot --headless --path demo res://tests/player_tests.tscn

var _failures := 0
var _watching := false

func _ready() -> void:
	await _test_scene("flat", "res://demo/flat.tscn", "JarVoxelVolume")
	await _test_scene("planet", "res://demo/planet.tscn", "Planet/JarVoxelVolume")
	await _test_moon()
	print("Player tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

func _check(name: String, ok: bool, detail := "") -> void:
	if not ok:
		_failures += 1
	print("%s %s %s" % ["PASS" if ok else "FAIL", name, detail])

func _test_scene(label: String, path: String, volume_path: String) -> void:
	var scene: Node = load(path).instantiate()
	add_child(scene)
	var player: DemoPlayer = scene.get_node("Player")
	var volume: JarVoxelVolume = scene.get_node(volume_path)
	_watching = true
	_watch_fall(label, player, volume)

	await _wait_until(func(): return player._spawned and player.is_on_floor(), 20000)
	_check("%s spawns on ground" % label, player.is_on_floor())
	var expected_up := volume.get_up_direction_at(player.global_position)
	_check("%s stands along gravity" % label, player.global_basis.y.dot(expected_up) > 0.999,
		"dot %.4f" % player.global_basis.y.dot(expected_up))

	# Walk, then sprint (Shift): the speed along the ground must reach the
	# configured value (terrain is hilly, so distance alone is not reliable).
	Input.action_press("move_forward")
	var walk_peak := await _peak_ground_speed(player, 40)
	_check("%s walks" % label, absf(walk_peak - player.walk_speed) < 0.5, "%.2f m/s" % walk_peak)
	# Sprint back over the stretch just walked, which is known to be walkable.
	player.rotate_object_local(Vector3.UP, PI)
	Input.action_press("move_down")
	var sprint_peak := await _peak_ground_speed(player, 40)
	Input.action_release("move_down")
	Input.action_release("move_forward")
	# Steep hills can cap the sprint on the planet.
	_check("%s sprints" % label, sprint_peak > player.sprint_speed * 0.8, "%.2f m/s" % sprint_peak)
	await _frames(60, func(): return player.is_on_floor() and player.velocity.length() < 0.1)

	# Jump: rises at least half a meter, then lands again.
	await _wait_until(func(): return player.is_on_floor(), 5000)
	var ground_height := player.global_position.dot(player.up_direction)
	Input.action_press("move_up")
	await _frames(30, func(): return not player.is_on_floor())
	Input.action_release("move_up")
	var peak := 0.0
	for i in 90:
		await get_tree().physics_frame
		peak = maxf(peak, player.global_position.dot(player.up_direction) - ground_height)
	_check("%s jumps" % label, peak > 0.5, "peak %.2f m" % peak)
	await _wait_until(func(): return player.is_on_floor(), 5000)
	_check("%s lands" % label, player.is_on_floor())

	# Dig straight down with the brush: the ground under the player drops.
	var editor: BrushEditor = player.get_node("Head/Camera3D/Editor")
	player.get_node("Head").rotation.x = -PI / 2 # look down
	await _frames(5)
	_watching = false # digging lowers the ground below the original SDF surface
	# Measure from a fixed point: the player falls into the hole it digs.
	var probe := player.global_position + player.up_direction * 1.0
	var before := _ground_distance(player, probe)
	Input.action_press("left_click")
	await _frames(20)
	Input.action_release("left_click")
	await _wait_until(func(): return not volume.is_building() and not volume.is_meshing(), 10000)
	await _wait_ms(300) # colliders upload over a few frames
	var after := _ground_distance(player, probe)
	_check("%s brush digs" % label, after > before + 0.5, "ground dropped %.2f m" % (after - before))
	player.get_node("Head").rotation.x = 0.0

	# Fly up for one second.
	player.flying = true
	var start := player.global_position
	Input.action_press("move_up")
	await _frames(60)
	Input.action_release("move_up")
	var climbed := (player.global_position - start).dot(player.up_direction)
	_check("%s flies" % label, climbed > 5.0, "climbed %.1f m" % climbed)

	scene.queue_free()
	await get_tree().process_frame

func _peak_ground_speed(player: DemoPlayer, frames: int) -> float:
	var peak := 0.0
	for i in frames:
		await get_tree().physics_frame
		peak = maxf(peak, player.velocity.slide(player.up_direction).length())
	return peak

## Distance from `from` down (along the player's up) to the ground.
func _ground_distance(player: DemoPlayer, from: Vector3) -> float:
	var query := PhysicsRayQueryParameters3D.create(from, from - player.up_direction * 50.0)
	query.exclude = [player.get_rid()]
	var hit := player.get_world_3d().direct_space_state.intersect_ray(query)
	return (from - hit.position).length() if hit else INF

## Stands on the orbiting, spinning moon: the player must stay on its surface
## (moving with it) instead of being left behind.
func _test_moon() -> void:
	var scene: Node = load("res://demo/planet.tscn").instantiate()
	var moon: Node3D = scene.get_node("MoonOrbit/Moon")
	var player: DemoPlayer = scene.get_node("Player")
	# Speed the moon up so a few seconds cover a lot of motion.
	# Fast, but the orbit's centripetal acceleration must stay well below the
	# moon's gravity (3 m/s²), or the player floats: ~60 m/s at 6 km.
	scene.get_node("MoonOrbit").degrees_per_second = 0.6
	moon.degrees_per_second = 10.0 # fast, but slow enough not to bounce off crater rims
	add_child(scene)
	await get_tree().physics_frame
	player.global_position = moon.global_position + Vector3.UP * 360.0
	await _wait_until(func(): return player._spawned and player.is_on_floor(), 20000)
	_check("moon spawns on moon", player.is_on_floor() and player.global_position.distance_to(moon.global_position) < 345.0,
		"distance to moon center %.1f" % player.global_position.distance_to(moon.global_position))
	var radius := player.global_position.distance_to(moon.global_position)
	var moved := 0.0
	var last := player.global_position
	var floor_frames := 0
	for i in 300:
		await get_tree().physics_frame
		moved += player.global_position.distance_to(last)
		last = player.global_position
		floor_frames += 1 if player.is_on_floor() else 0
	var drift := absf(player.global_position.distance_to(moon.global_position) - radius)
	_check("moon carries player", moved > 50.0 and drift < 3.0 and floor_frames > 280,
		"moved %.0f m, radius drift %.2f m, on floor %d/300 frames" % [moved, drift, floor_frames])
	scene.queue_free()
	await get_tree().process_frame

## Reports (once) if the player ever ends up well below the volume's surface.
func _watch_fall(label: String, player: DemoPlayer, volume: JarVoxelVolume) -> void:
	while is_instance_valid(player) and player.is_inside_tree():
		await get_tree().physics_frame
		if not _watching:
			return
		if not is_instance_valid(player) or not player._spawned or player.flying:
			continue
		var up := player.up_direction
		var surface := volume.raycast_sdf(player.global_position + up * 100.0, player.global_position - up * 100.0)
		if surface and (surface.position - player.global_position).dot(up) > 2.0:
			_check("%s stays above ground" % label, false, "fell through at %s, velocity %.1f, building=%s meshing=%s" % [
				player.global_position.round(), player.velocity.length(), volume.is_building(), volume.is_meshing()])
			return

## Background work (builds, meshing) runs on wall-clock time, while idle
## frames are almost free, so waits for it must be time-based.
func _wait_until(condition: Callable, timeout_ms: int) -> void:
	var start := Time.get_ticks_msec()
	while not condition.call() and Time.get_ticks_msec() - start < timeout_ms:
		await get_tree().physics_frame

func _wait_ms(ms: int) -> void:
	await _wait_until(func(): return false, ms)

func _frames(count: int, until := Callable()) -> void:
	for i in count:
		if until.is_valid() and until.call():
			return
		await get_tree().physics_frame
