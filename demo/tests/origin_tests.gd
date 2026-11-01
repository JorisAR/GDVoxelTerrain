extends Node
## Headless checks for the floating origin: once the viewer is far from the
## origin, the scene moves back near it, with nothing changing relative to the
## terrain. Run:
##   godot --headless --path demo res://tests/origin_tests.tscn

const DISTANCE_SETTING := "jar_voxel_terrain/floating_origin/distance"

var _failures := 0
var _shifts: Array = []

func _ready() -> void:
	JarVoxelUniverse.origin_shifted.connect(func(shift: Transform3D): _shifts.append(shift))
	await _test_small()
	await _test_flat()
	await _test_planet(false)
	await _test_planet(true)
	print("Origin tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

func _check(name: String, ok: bool, detail := "") -> void:
	if not ok:
		_failures += 1
	print("%s %s %s" % ["PASS" if ok else "FAIL", name, detail])

func _on_grid(v: Vector3, step: float) -> bool:
	return is_equal_approx(fposmod(v.x, step), 0.0) and is_equal_approx(fposmod(v.y, step), 0.0) and is_equal_approx(fposmod(v.z, step), 0.0)

## A small world never shifts.
func _test_small() -> void:
	_shifts.clear()
	var world := TestWorld.flat(self, Vector3(300, 30, -500))
	await _settle(world.volume)
	_check("small world: no shift", _shifts.is_empty() and world.volume.global_position == Vector3.ZERO,
		"%d shifts, volume at %s" % [_shifts.size(), world.volume.global_position])
	world.volume.queue_free()
	world.viewer.queue_free()
	await get_tree().process_frame

## Flat terrain, no universe: whole-meter shifts, and a shift alone (the
## viewer still where it was on the terrain) builds nothing.
func _test_flat() -> void:
	ProjectSettings.set_setting(DISTANCE_SETTING, 0.0) # off while placing
	var world := TestWorld.flat(self, Vector3(5000.4, 30, -3000.7))
	await _settle(world.volume)
	var volume: JarVoxelVolume = world.volume
	var viewer: Node3D = world.viewer
	var local := volume.to_local(viewer.global_position)
	_shifts.clear()
	ProjectSettings.set_setting(DISTANCE_SETTING, 2048.0)
	var rebuilt := false
	for i in 20:
		await get_tree().physics_frame
		await get_tree().process_frame
		rebuilt = rebuilt or volume.is_building()
	_check("flat: shifted once", _shifts.size() == 1, "%d shifts" % _shifts.size())
	_check("flat: viewer near the origin", viewer.global_position.length() < 1.0, "%s" % viewer.global_position)
	_check("flat: same place on the terrain", volume.to_local(viewer.global_position).distance_to(local) < 1e-3,
		"%s vs %s" % [volume.to_local(viewer.global_position), local])
	_check("flat: whole meters", _on_grid(volume.global_position, 1.0), "volume at %s" % volume.global_position)
	_check("flat: nothing rebuilt", not rebuilt)
	volume.queue_free()
	viewer.queue_free()
	await get_tree().process_frame

## The demo planet (and its moon): the scene follows the player back near the
## origin, the body it stands on with an identity basis on a 1/16 m grid.
func _test_planet(on_moon: bool) -> void:
	var label := "moon" if on_moon else "planet"
	var scene: Node = load("res://demo/planet.tscn").instantiate()
	var player = scene.get_node("Player")
	player.start_flying = true
	add_child(scene)
	var body: Node3D = scene.get_node("%Moon") if on_moon else scene.get_node("Sun/PlanetOrbit/Planet")
	var volume: JarVoxelVolume = body.get_node("JarVoxelVolume")
	var sdf: JarSignedDistanceField = volume.sdf
	var radius: float = sdf.radius if sdf is JarTerrainSdf else sdf.sdf.radius
	for i in 5:
		await get_tree().physics_frame
	# Far from the origin, above the surface (low enough to turn with the body).
	var dir := Vector3(0.6, -0.3, 0.74).normalized()
	var height := 150.0 if on_moon else 1500.0
	player.global_position = volume.global_transform * (dir * (radius + height))
	player.process_mode = Node.PROCESS_MODE_DISABLED # teleported: it would drift off
	# Now in the body's frame (a moon moves in its planet's): from here on,
	# a teleport to the far side keeps its place on the body.
	for i in 10:
		await get_tree().physics_frame
	player.global_position = volume.global_transform * (-dir * (radius + height))
	var local := volume.to_local(player.global_position)
	_shifts.clear()
	for i in 10:
		await get_tree().physics_frame
	_check("%s: shifted" % label, not _shifts.is_empty(), "%d shifts" % _shifts.size())
	_check("%s: player near the origin" % label, player.global_position.length() < 1.0, "%s" % player.global_position)
	_check("%s: same place on the body" % label, volume.to_local(player.global_position).distance_to(local) < 0.01,
		"%.4f m off" % volume.to_local(player.global_position).distance_to(local))
	var xform := volume.global_transform
	_check("%s: body basis exactly the identity" % label, xform.basis == Basis(), "%s" % xform.basis)
	_check("%s: body origin on a 1/16 m grid" % label, _on_grid(xform.origin, 1.0 / 16.0), "%s" % xform.origin)
	scene.queue_free()
	await get_tree().process_frame

func _settle(volume: JarVoxelVolume) -> void:
	var start := Time.get_ticks_msec()
	var idle := 0
	while idle < 30 and Time.get_ticks_msec() - start < 60000:
		await get_tree().physics_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0
