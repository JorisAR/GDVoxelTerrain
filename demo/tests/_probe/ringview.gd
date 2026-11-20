extends Node
## Screenshots of the ringed giant. VIEW=ground|space|back|edge, OUT=png, FOV.
func _ready() -> void:
	var scene: Node = load("res://demo/giant_planet.tscn").instantiate()
	var player := scene.get_node("Player")
	player.start_flying = true
	if OS.get_environment("SPACE") != "":
		scene.get_node("Sun/Light").space_min_sensitivity = float(OS.get_environment("SPACE"))
	if OS.get_environment("LINEAR") == "1":
		var we2: WorldEnvironment = scene.get_node("WorldEnvironment")
		we2.environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
		we2.environment.glow_enabled = false
	add_child(scene)
	var view := OS.get_environment("VIEW")
	var giant: JarCelestialBody = scene.get_node("Sun/RingedOrbit/RingedGiant")
	var sun: JarCelestialBody = scene.get_node("Sun")
	var cam := get_viewport().get_camera_3d()
	if view != "ground":
		player.process_mode = Node.PROCESS_MODE_DISABLED
	for i in 30:
		await get_tree().process_frame
	if OS.get_environment("MAXS") != "":
		var we: WorldEnvironment = scene.get_node("WorldEnvironment")
		we.camera_attributes.auto_exposure_max_sensitivity = float(OS.get_environment("MAXS"))
		if float(OS.get_environment("MAXS")) < 0.0:
			we.camera_attributes.auto_exposure_enabled = false
		print("attrs ", we.camera_attributes, " ", we.camera_attributes.auto_exposure_max_sensitivity, " cam attrs ", cam.attributes)
	for pair in OS.get_environment("RINGS").split(",", false):
		var rm: ShaderMaterial = scene.get_node("Sun/RingedOrbit/RingedGiant/Rings").get_active_material()
		rm.set_shader_parameter(pair.get_slice("=", 0), str_to_var(pair.get_slice("=", 1)))
	if OS.get_environment("FOV") != "":
		cam.fov = float(OS.get_environment("FOV"))
	var t := Time.get_ticks_msec()
	for i in 400:
		await get_tree().process_frame
		var c := giant.get_center()
		var R := giant.radius
		var n: Vector3 = ((giant.get_parent() as Node3D).global_basis * giant.spin_axis).normalized()
		var to_sun := (sun.get_center() - c).normalized()
		var side := n.cross(to_sun).normalized()
		var s := signf(n.dot(to_sun))
		var pos: Vector3
		match view:
			"lit": # the sunlit face of the rings, sun behind us
				pos = c + (to_sun * 0.8 + side * 0.6 + n * 0.45 * s).normalized() * R * 7.0
			"litback": # the sunlit face, looking towards the sun past the planet
				pos = c + (-to_sun + n * 0.5 * s + side * 0.2).normalized() * R * 6.0
			"crescent": # the night side, sun nearly behind the planet, below the rings' plane
				pos = c + (-to_sun * 0.9 + side * 0.45 - n * 0.15 * s).normalized() * R * 5.0
			"space": # sun behind us, above the ring plane
				pos = c + (to_sun * 0.8 + side * 0.6 + n * 0.45).normalized() * R * 7.0
			"back": # sun behind the planet, a little above the rings
				pos = c + (-to_sun + n * 0.25 + side * 0.15).normalized() * R * 6.0
			"edge": # nearly edge-on, lit side
				pos = c + (to_sun * 0.5 + side * 0.85 + n * 0.04).normalized() * R * 5.0
			"under": # unlit side of the rings
				pos = c + (to_sun * 0.6 + side * 0.6 - n * 0.5 * signf(n.dot(to_sun))).normalized() * R * 6.0
			"home": # the terrain planet from far, sun behind us, same size as the giant in "space"
				var planet: JarCelestialBody = scene.get_node("Sun/PlanetOrbit/Planet")
				c = planet.get_center()
				R = maxf(planet.radius, 300000.0)
				if i == 0: print("planet radius ", planet.radius)
				n = Vector3.UP
				to_sun = (sun.get_center() - c).normalized()
				side = n.cross(to_sun).normalized()
				pos = c + (to_sun * 0.8 + side * 0.6 + n * 0.45).normalized() * R * 7.0 * 2.33
			_:
				pos = Vector3()
		if view != "ground":
			cam.global_position = pos
			cam.look_at(c, n)
		else:
			cam.look_at(giant.get_center(), (cam.global_position - scene.get_node("Sun/PlanetOrbit/Planet").get_center()).normalized())
	var gc := giant.get_center()
	var gn: Vector3 = ((giant.get_parent() as Node3D).global_basis * giant.spin_axis).normalized()
	var ts := (sun.get_center() - gc).normalized()
	var te := (cam.global_position - gc).normalized()
	var rmat: ShaderMaterial = scene.get_node("Sun/RingedOrbit/RingedGiant/Rings").get_active_material()
	print("shader sun ", rmat.get_shader_parameter("sun_direction"), " normal ", rmat.get_shader_parameter("ring_normal"), " mine ", ts, " ", gn, " center ", rmat.get_shader_parameter("planet_center"), " gc ", gc)
	print("sun_side ", gn.dot(ts), " view_side ", gn.dot(te), " phase deg ", rad_to_deg(ts.angle_to(te)))
	await RenderingServer.frame_post_draw
	var img := get_viewport().get_texture().get_image()
	img.save_png(OS.get_environment("OUT"))
	print("ring pixels ", img.get_pixel(520, 380), img.get_pixel(760, 380), img.get_pixel(640, 405), " center pixel ", img.get_pixel(640, 330), " sky ", img.get_pixel(100, 600))
	print("saved ", OS.get_environment("OUT"), " cam far ", cam.far)
	get_tree().quit()
