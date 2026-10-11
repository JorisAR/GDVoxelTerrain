extends Node
func _ready() -> void:
	var scene: Node = load("res://demo/walk_planet.tscn").instantiate()
	add_child(scene)
	var player: DemoPlayer = scene.get_node("Player")
	var volume: JarVoxelVolume = scene.get_node("Planet/JarVoxelVolume")
	var t0 := Time.get_ticks_msec()
	while not (player._spawned and player.is_on_floor()) and Time.get_ticks_msec() - t0 < 20000:
		await get_tree().physics_frame
	Input.action_press("move_forward")
	Input.action_press("move_down")
	var falls := 0
	for i in 6000:
		await get_tree().physics_frame
		if i % 240 == 0:
			player.rotate_object_local(Vector3.UP, PI * 0.7)
		var surface := volume.raycast_sdf(player.global_position + player.up_direction * 200.0, player.global_position - player.up_direction * 200.0)
		if surface:
			var depth: float = (surface.position - player.global_position).dot(player.up_direction)
			if depth > 1.5:
				falls += 1
				var q := PhysicsRayQueryParameters3D.create(player.global_position + player.up_direction * 50, player.global_position - player.up_direction * 50)
				q.exclude = [player.get_rid()]
				var hit := player.get_world_3d().direct_space_state.intersect_ray(q)
				print("BELOW f%d depth=%.2f vel=%.1f floor=%s building=%s meshing=%s collider_hit_above=%s" % [i, depth, player.velocity.length(), player.is_on_floor(), volume.is_building(), volume.is_meshing(), hit.get("position", Vector3.INF).distance_to(player.global_position) < 60 and (hit.position - player.global_position).dot(player.up_direction) > 0 if hit else false])
				if falls > 5: break
	print("done falls=", falls, " wall_ms=", Time.get_ticks_msec() - t0)
	get_tree().quit()
