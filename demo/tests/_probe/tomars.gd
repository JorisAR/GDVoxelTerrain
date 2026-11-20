extends Node
## Aims at Mars once, holds forward at full speed, logs the drift.
func _ready() -> void:
	var scene: Node = load("res://demo/solar_system.tscn").instantiate()
	add_child(scene)
	var player = scene.get_node("Player")
	var t0 := Time.get_ticks_msec()
	while not player._spawned and Time.get_ticks_msec() - t0 < 60000:
		await get_tree().physics_frame
	player.flying = true
	player.fly_speed = player.max_fly_speed
	var mars: JarCelestialBody = scene.get_node("%Mars")
	var head: Node3D = player.head
	head.look_at(mars.get_center(), player.global_basis.y)
	Input.action_press("move_forward")
	for i in 60 * 40:
		await get_tree().physics_frame
		if i % 60 == 0 or (i < 90 and i % 6 == 0):
			var to := (mars.get_center() - head.global_position)
			var fwd := -head.global_basis.z
			var earth: JarCelestialBody = scene.get_node("%Planet")
			print("up %.1f vel-to-mars %.1f marsdir-change " % [rad_to_deg(player.global_basis.y.angle_to(head.global_position - earth.get_center())), rad_to_deg(player.velocity.angle_to(to))], mars.get_center().normalized(), " pos ", head.global_position.round())
			print("t %d dist %.0f km angle %.2f deg anchor %s rot %s near %.2f" % [i / 60, to.length() / 1000.0, rad_to_deg(fwd.angle_to(to)), JarVoxelUniverse.get_frame_anchor().name, JarVoxelUniverse.is_frame_rotating(), get_viewport().get_camera_3d().near])
	get_tree().quit()
