extends SceneTree
func _init() -> void:
	var t: JarTerrainSdf = load("res://demo/giant/giant_terrain.tres")
	var m: JarClimateBiomeMap = load("res://demo/giant/giant_biomes.tres")
	var base := Vector3(286010.1, 352562.9, 392337.1).normalized()
	var rng := RandomNumberGenerator.new()
	rng.seed = 9
	var best := []
	for i in 6000:
		var d := (base + Vector3(rng.randfn(), rng.randfn(), rng.randfn()) * 0.12).normalized()
		var p := d * t.radius
		var h := t.sample_height(p) - t.sea_level
		if h < 15.0 or h > 90.0:
			continue
		var temp: float = m.temperature_noise.get_noise_3dv(p) - 0.08 * h / 100.0 - m.latitude_effect * d.y * d.y
		var moist: float = m.moisture_noise.get_noise_3dv(p)
		var dt: Vector2 = Vector2(temp - 0.1, moist - 0.2)
		var w: float = exp(-dt.dot(dt) / (0.25 * 0.25))
		if w < 0.8:
			continue
		var sea := 1e9
		for a in 12:
			var dir: Vector3 = d.cross(Vector3(0.3, 1, 0)).normalized().rotated(d, a * TAU / 12.0)
			for s in [800.0, 1600.0, 3000.0]:
				var q: Vector3 = (d * t.radius + dir * s).normalized() * t.radius
				if t.sample_height(q) < t.sea_level:
					sea = min(sea, s)
		if sea > 3000.0:
			continue
		best.append([sea, w, h, p / t.radius * (t.radius + h + t.sea_level)])
	best.sort()
	print(best.size())
	for b in best.slice(0, 8):
		print(b)
	quit()
