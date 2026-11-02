extends Node
## Headless checks for the volume's surface queries (get_distance,
## project_to_surface, raycast, keep_above_surface): they agree with the
## colliders, see edits before any mesh does, and keep a body that outruns
## the colliders above ground. Run:
##   godot --headless --path demo res://tests/surface_tests.tscn

var _failures := 0

func _ready() -> void:
	await _flat_world()
	await _fast_fall()
	print("Surface tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

func _check(name: String, ok: bool, detail := "") -> void:
	if not ok:
		_failures += 1
	print("%s %s %s" % ["PASS" if ok else "FAIL", name, detail])

func _settle(volume: JarVoxelVolume) -> void:
	var start := Time.get_ticks_msec()
	var idle := 0
	while idle < 30 and Time.get_ticks_msec() - start < 60000:
		await get_tree().physics_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0

func _collider_height(volume: JarVoxelVolume, at: Vector3) -> float:
	var query := PhysicsRayQueryParameters3D.create(Vector3(at.x, 500, at.z), Vector3(at.x, -500, at.z))
	var hit := volume.get_world_3d().direct_space_state.intersect_ray(query)
	return hit.position.y if hit else NAN

func _flat_world() -> void:
	var holder := Node.new()
	add_child(holder)
	var world := TestWorld.flat(holder, Vector3(10, 60, -20))
	var volume: JarVoxelVolume = world.volume
	await _settle(volume)

	# The queries against the colliders (the mesh of the same field).
	var worst := 0.0
	for i in 12:
		var at := Vector3(10 + i * 3.1, 0, -20 + i * 1.7)
		var ground := _collider_height(volume, at)
		var hit := volume.raycast(Vector3(at.x, 200, at.z), Vector3(at.x, -200, at.z))
		worst = maxf(worst, absf(hit.position.y - ground) if hit else INF)
	_check("raycast matches the colliders", worst < 0.15, "largest difference %.3f m" % worst)

	var at := Vector3(14, 0, -18)
	var ground := _collider_height(volume, at)
	var above := Vector3(at.x, ground + 5.0, at.z)
	var projected := volume.project_to_surface(above)
	# The terrain's field is close to a distance, not exact.
	var expected: float = above.distance_to(projected.position) if projected else 5.0
	_check("distance above ground", absf(volume.get_distance(above) - expected) < 0.1 * expected,
		"%.3f (%.3f expected)" % [volume.get_distance(above), expected])
	# The nearest point on a slope is not straight below: on the surface there.
	_check("projects onto the ground", not projected.is_empty() and absf(volume.get_distance(projected.position)) < 0.02
		and absf(projected.position.y - _collider_height(volume, projected.position)) < 0.15 and projected.normal.y > 0.7,
		"%s" % projected)

	# A pit, seen as soon as the edit is applied (before it is meshed).
	var pit := Vector3(at.x, ground, at.z)
	volume.sphere_edit(pit, 3.0, false)
	while volume.is_building() or volume.get_distance(pit) < 1.0:
		await get_tree().process_frame
		if Engine.get_process_frames() > 100000:
			break
	var bottom := volume.raycast(pit + Vector3.UP * 20.0, pit + Vector3.DOWN * 20.0)
	_check("sees an edit at once", not bottom.is_empty() and bottom.position.y < ground - 2.5,
		"pit bottom %s, ground %.2f" % [bottom.get("position"), ground])

	# Buried bodies come out, without their velocity into the ground.
	await _settle(volume)
	var body := RigidBody3D.new()
	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	body.add_child(shape)
	holder.add_child(body)
	var spot := Vector3(at.x + 9.0, 0.0, at.z - 4.0)
	var spot_ground := _collider_height(volume, spot)
	body.global_position = Vector3(spot.x, spot_ground - 4.0, spot.z)
	body.linear_velocity = Vector3(0, -20, 0)
	var moved := volume.keep_above_surface(body, 0.5)
	var d := volume.get_distance(body.global_position)
	var normal: Vector3 = volume.project_to_surface(body.global_position).normal
	# (The normal here, 4 m from where it was corrected, differs a little.)
	_check("buried body comes out", moved and absf(d - 0.5) < 0.2 and body.linear_velocity.dot(normal) >= -1.0,
		"distance %.3f, velocity into the ground %.3f" % [d, -body.linear_velocity.dot(normal)])
	_check("a body above stays put", not volume.keep_above_surface(body, 0.1))
	holder.queue_free()
	await get_tree().process_frame

## A body falling at 1 km/s onto the 4 km planet, without continuous
## collision detection: it passes through colliders in one tick, but the
## surface query each tick keeps it above ground.
func _fast_fall() -> void:
	var sdf: JarTerrainSdf = load("res://tests/planet_terrain_4km.tres")
	var holder := Node3D.new()
	add_child(holder)
	var volume := JarVoxelVolume.new()
	volume.chunk_scene = TestWorld.CHUNK_SCENE
	volume.extent = 10000.0
	volume.sdf = sdf
	holder.add_child(volume)
	var up := Vector3(0.45, 0.55, 0.7).normalized()
	var ground := sdf.radius + sdf.sample_height(up * sdf.radius)
	var viewer := JarVoxelViewer.new()
	holder.add_child(viewer)
	viewer.global_position = volume.to_global(up * (ground + 5.0))
	await _settle(volume)
	var results := []
	for guarded in [false, true]:
		var body := RigidBody3D.new()
		body.gravity_scale = 0.0
		var shape := CollisionShape3D.new()
		shape.shape = SphereShape3D.new()
		body.add_child(shape)
		holder.add_child(body)
		body.global_position = volume.to_global(up * (ground + 300.0))
		body.linear_velocity = -(volume.global_transform.basis * up) * 1000.0
		for tick in 60:
			await get_tree().physics_frame
			if guarded:
				volume.keep_above_surface(body, 0.5)
		results.append(volume.get_distance(body.global_position))
		body.queue_free()
		await get_tree().physics_frame
	# Unguarded it passes through the colliders (the case this is for);
	# guarded it may slide along a slope, but stays above.
	_check("fast fall: unguarded body falls through", results[0] < -10.0, "%.1f m" % results[0])
	_check("fast fall: guarded body stays above ground", results[1] > 0.0, "%.2f m above" % results[1])
	holder.queue_free()
	await get_tree().process_frame
