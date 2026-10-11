extends Node
## Measures frame times of a demo scene from a given viewpoint, to find what
## costs FPS. Run windowed (rendering is part of the cost), vsync is turned off:
##   SCENE=res://demo/planet.tscn POS=x,y,z LOOK=x,y,z godot --path demo res://tests/visual/perf.tscn
## ON_MOON=1 puts the camera on the moon (which moves: colliders follow it).
## DISABLE=atmosphere,water,features,details,shadows,clouds switches parts off.
## Prints one PERF line.

func _ready() -> void:
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0
	var scene: Node = load(OS.get_environment("SCENE")).instantiate()
	var player := scene.get_node_or_null("Player")
	if player:
		player.start_flying = true
		player.process_mode = Node.PROCESS_MODE_DISABLED
	var disabled := OS.get_environment("DISABLE").split(",", false)
	for v: JarVoxelVolume in scene.find_children("*", "JarVoxelVolume", true, false):
		if "features" in disabled:
			v.population_terrain_features = []
		if "details" in disabled:
			v.population_terrain_details = []
	for name in ["Atmosphere", "Water", "Clouds"]:
		if name.to_lower() in disabled:
			for n in scene.find_children(name, "", true, false):
				n.queue_free()
	if "shadows" in disabled:
		for l: DirectionalLight3D in scene.find_children("*", "DirectionalLight3D", true, false):
			l.shadow_enabled = false
	add_child(scene)
	var camera: Camera3D = scene.find_children("*", "Camera3D", true, false)[0]
	var moon: Node3D = scene.get_node_or_null("MoonOrbit/Moon")
	var on_moon := OS.get_environment("ON_MOON") == "1" and moon != null
	var place := func():
		if on_moon:
			var up := moon.global_basis.y
			camera.global_position = moon.global_position + up * 340.0
			camera.look_at(moon.global_position + up * 300.0 + moon.global_basis.x * 200.0, up)
		else:
			camera.global_position = _vec(OS.get_environment("POS"))
			camera.look_at(_vec(OS.get_environment("LOOK")))
		if player:
			player.global_position = camera.global_position
	place.call()
	var volumes := scene.find_children("*", "JarVoxelVolume", true, false)
	var start := Time.get_ticks_msec()
	var idle := 0
	while idle < 60 and Time.get_ticks_msec() - start < 90000:
		await get_tree().process_frame
		place.call()
		var busy := false
		for v in volumes:
			busy = busy or v.is_building() or v.is_meshing()
		idle = 0 if busy else idle + 1
	# Let features and colliders finish streaming in.
	for i in 120:
		await get_tree().process_frame
		place.call()
	var frames: Array[float] = []
	var process := 0.0
	var physics := 0.0
	var last := Time.get_ticks_usec()
	for i in 300:
		await get_tree().process_frame
		place.call()
		var now := Time.get_ticks_usec()
		frames.append((now - last) / 1000.0)
		last = now
		process += Performance.get_monitor(Performance.TIME_PROCESS) * 1000.0
		physics += Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000.0
	frames.sort()
	var avg := 0.0
	for f in frames:
		avg += f
	avg /= frames.size()
	var draws := RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME)
	var prims := RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_PRIMITIVES_IN_FRAME)
	print("PERF %s avg_ms=%.2f fps=%.0f p95_ms=%.2f process_ms=%.2f physics_ms=%.2f draws=%d prims=%d" % [
		OS.get_environment("LABEL"), avg, 1000.0 / avg, frames[int(frames.size() * 0.95)], process / 300.0,
		physics / 300.0, draws, prims])
	get_tree().quit()

func _vec(s: String) -> Vector3:
	var p := s.split(",")
	return Vector3(float(p[0]), float(p[1]), float(p[2]))
