extends Node
func _ready() -> void:
	var scene: Node = load("res://demo/solar_system.tscn").instantiate()
	add_child(scene)
	var player = scene.get_node("Player")
	var t0 := Time.get_ticks_msec()
	while not player._spawned and Time.get_ticks_msec() - t0 < 60000:
		await get_tree().physics_frame
	for i in 30:
		await get_tree().process_frame
	var cam := get_viewport().get_camera_3d()
	var eye := cam.global_position
	var planet: JarCelestialBody = scene.get_node("%Planet")
	var up := (eye - planet.get_center()).normalized()
	print("spawned ", player._spawned, " altitude ", planet.get_altitude(eye), " far ", cam.far)
	for path in ["Sun", "%Moon", "%Mars", "%Jupiter", "%Saturn", "%Neptune"]:
		var b: JarCelestialBody = scene.get_node(path)
		var to := b.get_center() - eye
		var d := to.length()
		var r := b.get_effective_sea_level()
		var vr: float = b.get_view_radius()
		var view: float = JarVoxelUniverse.get_far_view_scale(b, cam)
		print("%-9s dist %9.0f km  diam %6.2f deg (view %6.2f)  elevation %6.1f deg  scale %f" % [b.name, d / 1000.0, rad_to_deg(2.0 * atan(r / d)), rad_to_deg(2.0 * atan(vr / d)), 90.0 - rad_to_deg(up.angle_to(to)), view])
	var mars: JarCelestialBody = scene.get_node("%Mars")
	var mars_sdf: JarTerrainSdf = (mars.get_node("JarVoxelVolume") as JarVoxelVolume).sdf.sdf
	var lo := 1e9
	var hi := -1e9
	var rng := RandomNumberGenerator.new()
	for i in 2000:
		var dir := Vector3(rng.randf_range(-1, 1), rng.randf_range(-1, 1), rng.randf_range(-1, 1)).normalized()
		var h := mars_sdf.sample_height(dir * mars_sdf.radius)
		lo = minf(lo, h)
		hi = maxf(hi, h)
	print("mars heights ", lo, " .. ", hi)
	var hops := ["%Planet", "%Mars", "%Jupiter", "%Saturn", "%Neptune"]
	for i in hops.size() - 1:
		var a: JarCelestialBody = scene.get_node(hops[i])
		var c: JarCelestialBody = scene.get_node(hops[i + 1])
		print("hop ", a.name, " -> ", c.name, " ", a.get_center().distance_to(c.get_center()) / 1000.0, " km")
	get_tree().quit()
