extends Node

func _ready() -> void:
	Engine.max_fps = 60
	var scene: Node = load("res://demo/planet.tscn").instantiate()
	var player: DemoPlayer = scene.get_node("Player")
	add_child(scene)
	var volume: JarVoxelVolume = scene.get_node("%Planet/JarVoxelVolume")
	var sdf: JarTerrainSdf = volume.sdf
	while not player._spawned:
		await get_tree().physics_frame
	while volume.is_building() or volume.is_meshing():
		await get_tree().process_frame
	player.set_physics_process(false)
	var dir := (volume.global_transform.affine_inverse() * player.global_position).normalized()
	var rng := RandomNumberGenerator.new()
	rng.seed = 3
	while sdf.sample_height(dir * sdf.radius) < 30.0:
		dir = (dir + Vector3(rng.randf_range(-1, 1), rng.randf_range(-1, 1), rng.randf_range(-1, 1)) * 0.01).normalized()
	var altitude := 40.0
	for phase in [6000.0, -6000.0]:
		var t := 0.0
		while t < 7.0:
			await get_tree().process_frame
			var d := get_process_delta_time()
			t += d
			altitude = maxf(40.0, altitude + phase * d)
			var h := maxf(sdf.sample_height(dir * sdf.radius), 0.0)
			player.global_position = volume.global_transform * (dir * (sdf.radius + h + altitude))
	var h0 := maxf(sdf.sample_height(dir * sdf.radius), 0.0)
	printerr("HEIGHT under the player: %.1f m" % sdf.sample_height(dir * sdf.radius))
	var idle := 0
	for i in 1200:
		player.global_position = volume.global_transform * (dir * (sdf.radius + h0 + 40.0))
		await get_tree().process_frame
		if i % 60 == 0:
			var lmin := 99
			var ppos2 := volume.to_local(player.global_position)
			for c in volume.get_children():
				if c is JarVoxelChunk and c.visible and c.get_center().distance_to(ppos2) < 300.0:
					lmin = mini(lmin, c.get_lod())
			printerr("WAIT %d building %s meshing %s finest lod within 300 m: %d, viewer local %s, extent %.0f" % [i, volume.is_building(), volume.is_meshing(), lmin, ppos2, volume.extent])
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0
		if idle > 30:
			break
	player.global_position = volume.global_transform * (dir * (sdf.radius + h0 + 40.0))
	await get_tree().physics_frame
	var bad := 0
	var shown := 0
	for c in volume.get_children():
		if c is JarVoxelChunk and c.visible:
			shown += 1
			if not c.transform.basis.is_equal_approx(Basis()) or c.is_set_as_top_level():
				bad += 1
	var up: Vector3 = volume.global_transform.basis * dir
	var q := PhysicsRayQueryParameters3D.create(player.global_position + up * 10.0, player.global_position - up * 200.0)
	q.exclude = [player.get_rid()]
	var hit := volume.get_world_3d().direct_space_state.intersect_ray(q)
	var active := 0
	var top := 0
	var near := 0
	var ppos := volume.to_local(player.global_position)
	for c in volume.get_children():
		if c is JarVoxelChunk and c.visible:
			var on: bool = not c.get_collision_shape().disabled
			if on:
				active += 1
				if c.get_static_body().is_set_as_top_level():
					top += 1
			if c.get_center().distance_to(ppos) < 80.0:
				near += 1
				printerr("  near chunk lod %d dist %.0f collider %s body top %s body pos %s chunk pos %s" % [c.get_lod(), c.get_center().distance_to(ppos), on, c.get_static_body().is_set_as_top_level(), c.get_static_body().global_position, c.global_position])
	printerr("UPDOWN colliders active %d (top level %d), near %d" % [active, top, near])
	printerr("UPDOWN shown %d, bad transforms %d, ground hit %s" % [shown, bad, hit.size() > 0])
	get_tree().quit()
