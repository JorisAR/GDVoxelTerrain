extends Node
## Headless checks for JarVoxelDebris: terrain an edit cuts loose falls as a
## rigid body and merges back into the ground where it comes to rest. Run:
##   godot --headless --path demo res://tests/debris_tests.tscn

var _failures := 0
var _events := {detached = [], settled = [], merged = []}

func _ready() -> void:
	await _test_floating_blob()
	await _test_pillar()
	await _test_disabled()
	print("Debris tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

func _check(name: String, ok: bool, detail := "") -> void:
	if not ok:
		_failures += 1
	print("%s %s %s" % ["PASS" if ok else "FAIL", name, detail])

## A volume with debris, around `at`; returns {volume, viewer, debris, ground}.
func _world(at: Vector3, enabled := true) -> Dictionary:
	for key in _events:
		_events[key] = []
	var world := TestWorld.flat(self, at + Vector3(0, 10, 0))
	var debris := JarVoxelDebris.new()
	debris.enabled = enabled
	debris.merge_delay = 0.3
	debris.debris_detached.connect(func(b): _events.detached.append(b))
	debris.debris_settled.connect(func(b): _events.settled.append(b))
	debris.debris_merged.connect(func(b): _events.merged.append(b.global_position))
	world.volume.add_child(debris)
	world.debris = debris
	await _settle(world.volume)
	world.ground = _ray_height(world.volume, at)
	world.viewer.position = Vector3(at.x, world.ground + 10, at.z)
	world.volume.force_update_lod()
	await _settle(world.volume)
	return world

## Floating material falls, lands and becomes part of the ground again.
func _test_floating_blob() -> void:
	var at := Vector3(4.3, 0, 6.7)
	var world := await _world(at)
	var volume: JarVoxelVolume = world.volume
	# A flat slab, dropped a little: it lands and stays (a ball, or a block
	# falling far, tumbles down the test terrain's hills).
	var blob := JarBoxSdf.new()
	blob.extent = Vector3(3.0, 1.0, 3.0)
	var center := Vector3(at.x, world.ground + 7.0, at.z)
	volume.edit(blob, JarVoxelVolume.SDF_OPERATION_UNION, Transform3D(Basis(), center))
	await _wait_until(func(): return _events.detached.size() > 0, 5000)
	_check("floating blob breaks off", _events.detached.size() == 1, "%d pieces" % _events.detached.size())
	if _events.detached.is_empty():
		await _cleanup(world)
		return
	var body: JarVoxelDebrisBody = _events.detached[0]
	_check("body where the blob was", body.global_position.distance_to(center) < 0.5,
		"%.2f m off" % body.global_position.distance_to(center))
	_check("blob gone from the terrain", _ray_height(volume, center + Vector3(0.4, 0, 0.3)) < world.ground + 1.0,
		"ray hits at %.2f, ground %.2f" % [_ray_height(volume, center + Vector3(0.4, 0, 0.3)), world.ground])
	_check("body has mass", body.mass > 1.0, "%.0f kg" % body.mass)

	await _wait_until(func(): return _events.merged.size() > 0, 15000)
	_check("blob settles", _events.settled.size() >= 1)
	_check("blob merges back", _events.merged.size() == 1)
	await _settle(volume)
	await get_tree().process_frame
	_check("no bodies left", world.debris.get_bodies().is_empty())
	# Merged where it landed: the ground there is higher now.
	var landed: Vector3 = _events.merged[0] if _events.merged.size() > 0 else center
	var below := _ray_height(volume, landed + Vector3(5.0, 0, 0)) # the ground beside it
	var top := _ray_height(volume, landed)
	_check("ground raised where it landed", top > below + 1.2, "%.2f above the ground beside it" % (top - below))
	var r := Topology.analyze(volume, landed, 8.0)
	_check("merged without holes", r.open == 0, "%d open edges" % r.open)
	await _cleanup(world)

## Terrain attached to the ground stays; cut through, the top falls.
func _test_pillar() -> void:
	var at := Vector3(-20.3, 0, 4.1)
	var world := await _world(at)
	var volume: JarVoxelVolume = world.volume
	var pillar := JarBoxSdf.new()
	pillar.extent = Vector3(1.5, 5.0, 1.5)
	volume.edit(pillar, JarVoxelVolume.SDF_OPERATION_UNION, Transform3D(Basis(), Vector3(at.x, world.ground + 4.0, at.z)))
	await _settle(volume)
	await _frames(10)
	_check("standing pillar stays", _events.detached.is_empty(), "%d pieces" % _events.detached.size())
	var slab := JarBoxSdf.new()
	slab.extent = Vector3(3.0, 0.8, 3.0)
	volume.edit(slab, JarVoxelVolume.SDF_OPERATION_SUBTRACTION, Transform3D(Basis(), Vector3(at.x, world.ground + 2.5, at.z)))
	await _wait_until(func(): return _events.detached.size() > 0, 5000)
	_check("cut pillar top falls", _events.detached.size() == 1, "%d pieces" % _events.detached.size())
	await _cleanup(world)

## Without `enabled`, nothing falls.
func _test_disabled() -> void:
	var at := Vector3(12.3, 0, -9.7)
	var world := await _world(at, false)
	var volume: JarVoxelVolume = world.volume
	var blob := JarSphereSdf.new()
	blob.radius = 2.0
	volume.edit(blob, JarVoxelVolume.SDF_OPERATION_UNION, Transform3D(Basis(), Vector3(at.x, world.ground + 10.0, at.z)))
	await _settle(volume)
	await _frames(10)
	_check("disabled: nothing falls", _events.detached.is_empty())
	await _cleanup(world)

func _cleanup(world: Dictionary) -> void:
	world.volume.queue_free()
	world.viewer.queue_free()
	await get_tree().process_frame

## Height of the terrain (debris bodies ignored) below `at`.
func _ray_height(volume: JarVoxelVolume, at: Vector3) -> float:
	var query := PhysicsRayQueryParameters3D.create(Vector3(at.x, 500, at.z), Vector3(at.x, -500, at.z))
	var exclude: Array[RID] = []
	for debris in volume.find_children("*", "JarVoxelDebris", false, false):
		for body in debris.get_bodies():
			exclude.append(body.get_rid())
	query.exclude = exclude
	var hit := volume.get_world_3d().direct_space_state.intersect_ray(query)
	return hit.position.y if hit else NAN

func _settle(volume: JarVoxelVolume) -> void:
	var start := Time.get_ticks_msec()
	var idle := 0
	while idle < 30 and Time.get_ticks_msec() - start < 60000:
		await get_tree().physics_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0
	await get_tree().create_timer(0.3).timeout # colliders upload over a few frames

func _frames(n: int) -> void:
	for i in n:
		await get_tree().physics_frame

func _wait_until(condition: Callable, timeout_ms: int) -> void:
	var start := Time.get_ticks_msec()
	while not condition.call() and Time.get_ticks_msec() - start < timeout_ms:
		await get_tree().physics_frame
