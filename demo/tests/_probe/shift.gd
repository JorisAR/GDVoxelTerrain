extends Node
## Stands on Earth in the solar system; saves frames 2 s apart (OUT_A, OUT_B) to diff.
func _ready() -> void:
	var scene: Node = load("res://demo/solar_system.tscn").instantiate()
	var player = scene.get_node("Player")
	if OS.get_environment("NOCLOUDS") == "1":
		for a in scene.find_children("*", "JarBodyClouds", true, false):
			a.free()
	add_child(scene)
	var t0 := Time.get_ticks_msec()
	while not player._spawned and Time.get_ticks_msec() - t0 < 60000:
		await get_tree().physics_frame
	for i in 400:
		await get_tree().process_frame
	var cam := get_viewport().get_camera_3d()
	var shifts := [0]
	JarVoxelUniverse.origin_shifted.connect(func(_s): shifts[0] += 1)
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(OS.get_environment("OUT_A"))
	var p0: Vector3 = player.global_position
	var planet: JarCelestialBody = scene.get_node("%Planet")
	var c0 := planet.get_center()
	var b0 := planet.global_basis
	for i in 120:
		await get_tree().process_frame
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(OS.get_environment("OUT_B"))
	print("player moved ", player.global_position.distance_to(p0), " planet center moved ", planet.get_center().distance_to(c0), " planet turned ", b0.get_rotation_quaternion().angle_to(planet.global_basis.get_rotation_quaternion()), " shifts ", shifts[0], " rotating ", JarVoxelUniverse.is_frame_rotating(), " anchor ", JarVoxelUniverse.get_frame_anchor().name, " near ", cam.near)
	get_tree().quit()
