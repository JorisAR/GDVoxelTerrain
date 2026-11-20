extends Node
## Mars through a 3 degree lens from Earth's surface, the camera sliding 2 m
## per frame: how much the disc changes from frame to frame.
func _ready() -> void:
	var scene: Node = load("res://demo/solar_system.tscn").instantiate()
	if OS.get_environment("NOMARSATMO") == "1":
		scene.get_node("Sun/MarsOrbit/Mars/Atmosphere").free()
	if OS.get_environment("NOCLOUDS") == "1":
		for a in scene.find_children("*", "JarBodyClouds", true, false):
			a.free()
	var player = scene.get_node("Player")
	add_child(scene)
	var t0 := Time.get_ticks_msec()
	while not player._spawned and Time.get_ticks_msec() - t0 < 60000:
		await get_tree().physics_frame
	player.set_physics_process(false)
	player.set_process(false)
	var cam := get_viewport().get_camera_3d()
	var mars: JarCelestialBody = scene.get_node("%Mars")
	cam.fov = 40.0
	var sun: JarCelestialBody = scene.get_node("Sun")
	for i in 600:
		await get_tree().process_frame
		var ts := (sun.get_center() - mars.get_center()).normalized()
		var side := ts.cross(Vector3.UP).normalized()
		var mv: JarVoxelVolume = mars.get_node("JarVoxelVolume")
		var msdf: JarTerrainSdf = mv.sdf.sdf
		var ldir := (mv.global_transform.basis.inverse() * (ts * 0.7 + side * 0.7).normalized()).normalized()
		var h: float = msdf.sample_height(ldir * msdf.radius)
		var p: Vector3 = mv.global_transform * (ldir * (msdf.radius + h + 30.0))
		var up: Vector3 = (p - mars.get_center()).normalized()
		cam.global_position = p
		player.global_position = p
		cam.look_at(p + up.cross(Vector3.RIGHT).normalized() * 100.0 + up * 3.0, up)
	var prev: Image = null
	var total := 0.0
	var worst := 0.0
	var step := Vector3(2, 0, 0)
	for i in 30:
		cam.global_position += cam.global_basis.x * 1.0
		player.global_position = cam.global_position
		await RenderingServer.frame_post_draw
		var img := get_viewport().get_texture().get_image()
		if i == 0:
			img.save_png(OS.get_environment("OUT"))
		if i < 8:
			print("FRAME ", i, " disc ", img.get_pixel(640, 360), " sky ", img.get_pixel(200, 600), " edge ", img.get_pixel(450, 360))
		if prev:
			var d := 0.0
			var n := 0
			for y in range(40, 330, 4):
				for x in range(40, 1240, 8):
					var a := img.get_pixel(x, y)
					var b := prev.get_pixel(x, y)
					d += absf(a.r - b.r) + absf(a.g - b.g) + absf(a.b - b.b)
					n += 1
			d /= n
			total += d
			worst = maxf(worst, d)
		prev = img
	print("FLICKER mean %.5f worst %.5f" % [total / 29.0, worst])
	get_tree().quit()
