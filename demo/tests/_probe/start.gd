extends Node
func _ready() -> void:
	var scene: Node = load("res://demo/solar_system.tscn").instantiate()
	scene.get_node("Player").process_mode = Node.PROCESS_MODE_DISABLED
	add_child(scene)
	for i in 3:
		await get_tree().physics_frame
	var planet: JarCelestialBody = scene.get_node("%Planet")
	var volume: JarVoxelVolume = planet.get_node("JarVoxelVolume")
	var sdf: JarTerrainSdf = volume.sdf
	var c := planet.get_center()
	var R := planet.get_effective_sea_level()
	var targets := {}
	for n in ["Sun", "%Mars", "%Jupiter", "%Saturn", "%Neptune"]:
		targets[n] = (scene.get_node(n) as JarCelestialBody).get_center()
	var best := -1e9
	var best_u := Vector3.UP
	var best_info := ""
	var count := 40000
	for i in count:
		var y := 1.0 - 2.0 * (i + 0.5) / count
		var rr := sqrt(1.0 - y * y)
		var phi := i * 2.399963
		var u := Vector3(cos(phi) * rr, y, sin(phi) * rr)
		var p := c + u * R
		var el := {}
		for n in targets:
			el[n] = 90.0 - rad_to_deg(u.angle_to(targets[n] - p))
		var sun: float = el["Sun"]
		if sun < 6.0 or sun > 22.0:
			continue
		var j: float = el["%Jupiter"]
		var s: float = el["%Saturn"]
		var m: float = el["%Mars"]
		var score := -absf(j - 35.0) - absf(s - 30.0) * 0.7 - absf(m - 25.0) * 0.5 - absf(sun - 14.0) * 0.5
		if score <= best:
			continue
		var local := volume.global_transform.affine_inverse() * p - volume.global_transform.affine_inverse() * c
		var h := sdf.sample_height(local.normalized() * sdf.radius)
		if h < 15.0:
			continue
		best = score
		best_u = u
		best_info = "sun %.1f mars %.1f jupiter %.1f saturn %.1f neptune %.1f height %.0f" % [sun, m, j, s, el["%Neptune"], h]
	var start := c + best_u * (R + 400.0)
	print("BEST ", best_info, " start ", start, " planet center ", c, " volume local ", volume.global_transform.affine_inverse() * start, " volume scene basis ", volume.global_transform.basis)
	get_tree().quit()
