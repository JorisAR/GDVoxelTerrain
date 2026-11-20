extends Node
## Screenshots of the solar system. VIEW: ground | horizon | earth | mars | jupiter | saturn | neptune | low (clouds from 1 km under), OUT, FRAMES.
func OCCLUDER_SPEED() -> float:
	return float(OS.get_environment("OCCLUDER"))

func _ready() -> void:
	var scene: Node = load("res://demo/solar_system.tscn").instantiate()
	var player = scene.get_node("Player")
	if OS.get_environment("NOATMO") == "1":
		for a in scene.find_children("*", "JarBodyAtmosphere", true, false):
			a.free()
	if OS.get_environment("NOWATER") == "1":
		for a in scene.find_children("*", "JarBodyWater", true, false):
			a.free()
	if OS.get_environment("NOCLOUDS") == "1":
		for a in scene.find_children("*", "JarBodyClouds", true, false):
			a.free()
	if OS.get_environment("HOLES") == "1":
		var we: WorldEnvironment = scene.get_node("WorldEnvironment")
		we.environment.background_mode = Environment.BG_COLOR
		we.environment.background_color = Color(1, 0, 1)
		we.environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
		we.environment.glow_enabled = false
		we.camera_attributes = null
		for f in scene.find_children("*", "JarLensFlare", true, false):
			f.free()
	add_child(scene)
	var view := OS.get_environment("VIEW")
	if OS.get_environment("WEATHER_PERIOD") != "":
		(scene.get_node("%Planet/Clouds") as JarBodyClouds).weather_period = float(OS.get_environment("WEATHER_PERIOD"))
	for pair in OS.get_environment("CLOUDS").split(",", false):
		(scene.get_node("%Planet/Clouds") as JarBodyClouds).get_active_material().set_shader_parameter(pair.get_slice("=", 0), str_to_var(pair.get_slice("=", 1)))
	var t0 := Time.get_ticks_msec()
	while not player._spawned and Time.get_ticks_msec() - t0 < 60000:
		await get_tree().physics_frame
	var cam := get_viewport().get_camera_3d()
	RenderingServer.viewport_set_measure_render_time(get_viewport().get_viewport_rid(), true)
	var space := view in ["marsmount", "marsground", "earth", "mars", "jupiter", "saturn", "neptune", "earthlimb", "belt"]
	if space or view == "low" or view == "aerial":
		# Only the player's own script: its viewer child must keep driving the LOD.
		player.set_physics_process(false)
		player.set_process(false)
		player.set_process_input(false)
		player.set_process_unhandled_input(false)
	var sun: JarCelestialBody = scene.get_node("Sun")
	var frames := int(OS.get_environment("FRAMES")) if OS.get_environment("FRAMES") != "" else 900
	var gpu := 0.0
	var n := 0
	for i in frames:
		await get_tree().process_frame
		var planet: JarCelestialBody = scene.get_node("%Planet")
		var c := planet.get_center()
		var up := (cam.global_position - c).normalized()
		if view == "belt":
			var sc := sun.get_center()
			var md := ((scene.get_node("%Mars") as JarCelestialBody).get_center() - sc).normalized()
			var p := sc + md * 103.0e6
			if i % 30 == 0:
				player.global_position = p
				cam.global_position = p
			var ts := (sc - cam.global_position).normalized()
			cam.look_at(cam.global_position + ts.cross(Vector3.UP).normalized() * 0.8 + ts * 0.6, Vector3.UP)
			if i == frames - 1:
				var belt = scene.get_node("Sun/Asteroids")
				print("ROCKS ", belt.get_rock_count(), " profile pos ", cam.global_position.distance_to(sc))
			continue
		match view:
			"ground":
				cam.look_at((scene.get_node("%Saturn") as JarCelestialBody).get_center(), up)
			"horizon":
				var to_sun := (sun.get_center() - cam.global_position).normalized()
				var flat := (to_sun - up * to_sun.dot(up)).normalized()
				cam.look_at(cam.global_position + flat.rotated(up, deg_to_rad(70.0)) + up * 0.12, up)
			"marsmount":
				var mars2: JarCelestialBody = scene.get_node("%Mars")
				var mv2: JarVoxelVolume = mars2.get_node("JarVoxelVolume")
				var msdf2: JarTerrainSdf = mv2.sdf.sdf
				if i == 0:
					var best := -1e9
					var rng := RandomNumberGenerator.new()
					rng.seed = 5
					var sun_local: Vector3 = (mv2.global_transform.basis.inverse() * (sun.get_center() - mars2.get_center())).normalized()
					for k in 3000:
						var d := Vector3(rng.randf_range(-1, 1), rng.randf_range(-1, 1), rng.randf_range(-1, 1)).normalized()
						if d.dot(sun_local) < 0.6:
							continue
						var hh := msdf2.sample_height(d * msdf2.radius)
						if hh > best:
							best = hh
							set_meta("mdir", d)
							set_meta("mh", hh)
				var md: Vector3 = get_meta("mdir")
				var mh: float = get_meta("mh")
				var lp: Vector3 = md * (msdf2.radius + mh + 1500.0)
				var p2: Vector3 = mv2.global_transform * lp
				var up2: Vector3 = (p2 - mars2.get_center()).normalized()
				var fwd: Vector3 = up2.cross(Vector3.RIGHT).normalized()
				player.global_position = p2
				cam.global_position = p2
				cam.look_at(p2 + fwd * 10.0 - up2 * 6.0, up2)
			"marsground":
				var mars: JarCelestialBody = scene.get_node("%Mars")
				var mv: JarVoxelVolume = mars.get_node("JarVoxelVolume")
				var msdf: JarTerrainSdf = mv.sdf.sdf
				var ldir := Vector3(0.3, 0.8, 0.2).normalized()
				var h: float = msdf.sample_height(ldir * msdf.radius)
				var p: Vector3 = mv.global_transform * (ldir * (msdf.radius + h + 2.0))
				var mup := (p - mars.get_center()).normalized()
				player.global_position = p
				cam.global_position = p
				var side := mup.cross(Vector3.RIGHT).normalized()
				cam.look_at(p + side * 10.0 - mup * 1.5, mup)
				if i == frames - 1:
					print("mars ground height ", h, " near ", cam.near)
			"aerial":
				if OS.get_environment("NIGHT") != "" and i == 0: # the night side (NIGHT: degrees past the terminator)
					var away := (c - sun.get_center()).normalized()
					var tangent := away.cross(Vector3.UP).normalized()
					up = away.rotated(tangent, deg_to_rad(90.0 - float(OS.get_environment("NIGHT"))))
				var pa := c + up * (planet.get_effective_sea_level() + float(OS.get_environment("ALT") if OS.get_environment("ALT") != "" else "25000"))
				player.global_position = pa
				cam.global_position = pa
				var ts := (sun.get_center() - pa).normalized()
				var fl := (ts - up * ts.dot(up)).normalized()
				if OS.get_environment("MOVE") != "": # meters per frame, sideways (ghosting check)
					pa += fl.rotated(up, deg_to_rad(210.0)) * float(OS.get_environment("MOVE"))
					player.global_position = pa
					cam.global_position = pa
				cam.look_at(pa + fl.rotated(up, deg_to_rad(120.0)) - up * 0.6, up)
			"low":
				var p := c + up * (planet.get_effective_sea_level() + (float(OS.get_environment("ALT")) if OS.get_environment("ALT") != "" else 1200.0))
				player.global_position = p
				cam.global_position = p
				var to_sun2 := (sun.get_center() - p).normalized()
				var flat2 := (to_sun2 - up * to_sun2.dot(up)).normalized()
				if OS.get_environment("MOVE") != "": # meters per frame, sideways (ghosting check)
					p += flat2.rotated(up, deg_to_rad(150.0)) * float(OS.get_environment("MOVE"))
					player.global_position = p
					cam.global_position = p
				var lift := float(OS.get_environment("LIFT")) if OS.get_environment("LIFT") != "" else 0.35
				cam.look_at(p + flat2.rotated(up, deg_to_rad(60.0)) + up * lift, up)
			_:
				if space:
					var target_path: String = {"earth": "%Planet", "earthlimb": "%Planet", "mars": "%Mars", "jupiter": "%Jupiter", "saturn": "%Saturn", "neptune": "%Neptune"}[view]
					var b: JarCelestialBody = scene.get_node(target_path)
					var bc := b.get_center()
					var R := b.get_effective_sea_level()
					var ts := (sun.get_center() - bc).normalized()
					var side := ts.cross(Vector3.UP).normalized()
					var dist := (float(OS.get_environment("DIST")) if OS.get_environment("DIST") != "" else 3.2) if view != "earthlimb" else 1.25
					var dir := (ts * 0.75 + side * 0.65 + Vector3.UP * 0.2).normalized()
					if view == "saturn":
						dir = (ts * 0.75 + side * 0.65 + Vector3.UP * 0.45).normalized()
						dist = 6.0
					var p := bc + dir * R * dist
					player.global_position = p
					cam.global_position = p
					if view == "earthlimb":
						cam.look_at(bc + side * R * 1.1, Vector3.UP)
					else:
						cam.look_at(bc, Vector3.UP)
		if OS.get_environment("OCCLUDER") != "": # a tall box sliding across the view (OCCLUDER: meters per frame)
			var box: MeshInstance3D = get_node_or_null("Occluder")
			if box == null:
				box = MeshInstance3D.new()
				box.name = "Occluder"
				var bm := BoxMesh.new()
				bm.size = Vector3(60, 1200, 60)
				box.mesh = bm
				add_child(box)
			var fwd := -cam.global_basis.z
			var side := cam.global_basis.x
			var sweep := fmod(float(i) * float(OCCLUDER_SPEED()), 1600.0) - 800.0
			box.global_position = cam.global_position + fwd * 900.0 + side * sweep
			box.global_basis = cam.global_basis
		if i > frames - 60:
			gpu += RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid())
			n += 1
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(OS.get_environment("OUT"))
	if OS.get_environment("CLOUDCOST") == "1": # GPU time with and without the clouds, same view
		for round in 3:
			var with_ms := 0.0
			var passes_ms := 0.0
			for k in 90:
				await get_tree().process_frame
				if k >= 30:
					with_ms += RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid()) / 60.0
					if ClassDB.class_exists("JarCloudsEffect"):
						passes_ms += ClassDB.class_call_static("JarCloudsEffect", "get_gpu_msec") / 60.0
			var layer: Node = scene.get_node("%Planet/Clouds")
			var parent := layer.get_parent()
			parent.remove_child(layer)
			var without_ms := 0.0
			for k in 90:
				await get_tree().process_frame
				if k >= 30:
					without_ms += RenderingServer.viewport_get_measured_render_time_gpu(get_viewport().get_viewport_rid()) / 60.0
			parent.add_child(layer)
			print("CLOUDCOST %.3f ms (with %.3f, without %.3f) compute passes %.3f ms" % [with_ms - without_ms, with_ms, without_ms, passes_ms])
	var img := get_viewport().get_texture().get_image()
	if view.begins_with("mars"):
		var mvol: JarVoxelVolume = scene.get_node("%Mars/JarVoxelVolume")
		var shown := 0
		for ch in mvol.get_children():
			if ch is JarVoxelChunk and ch.visible:
				shown += 1
		var viewers := get_tree().get_nodes_in_group("jar_voxel_viewer")
		print("MARS chunks ", shown, " building ", mvol.is_building(), " meshing ", mvol.is_meshing(), " lod at cam ", mvol.get_lod_at(cam.global_position), " viewers ", viewers.size(), " viewer pos ", (viewers[0] as Node3D).global_position.distance_to(cam.global_position) if viewers.size() > 0 else -1.0, " anchor ", JarVoxelUniverse.get_frame_anchor().name)
	if OS.get_environment("HOLES") == "1":
		var holes := 0
		for y in range(img.get_height() / 2, img.get_height()):
			for x in img.get_width():
				var c := img.get_pixel(x, y)
				if c.r > 0.9 and c.b > 0.9 and c.g < 0.15:
					holes += 1
		print("HOLE_PIXELS ", holes)
	print("center ", img.get_pixel(640, 360))
	if OS.get_environment("NIGHT") != "":
		for m in get_tree().get_nodes_in_group("jar_moon_light"):
			print("MOON ", m.light_color, " energy ", m.light_energy, " visible ", m.visible)
		var env: Environment = scene.get_node("WorldEnvironment").environment
		print("AMBIENT ", env.ambient_light_color, " x ", env.ambient_light_energy, " floor ", env.get_meta("jar_ambient_floor", "none"))
		var ca = cam.attributes if cam.attributes else scene.get_node("WorldEnvironment").camera_attributes
		if ca:
			print("EXPOSURE auto ", ca.auto_exposure_enabled, " mult ", ca.exposure_multiplier, " ", ca.get_class())
		for l in scene.find_children("*", "DirectionalLight3D", true, false):
			print("LIGHT ", l.name, " ", l.light_color, " e ", l.light_energy, " vis ", l.visible)
	print("saved ", view, " gpu ms ", gpu / max(n, 1), " far ", cam.far)
	get_tree().quit()
