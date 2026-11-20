extends SceneTree
func stats(t: JarTerrainSdf, label: String) -> void:
	var rng := RandomNumberGenerator.new()
	rng.seed = 5
	var slopes := {}
	for step: float in [10.0, 50.0, 250.0]:
		var sum := 0.0
		var n := 0
		for i in 3000:
			var p := Vector3(rng.randfn(), rng.randfn(), rng.randfn()).normalized() * t.radius
			var h := t.sample_height(p)
			if h < t.sea_level or h > 500.0:
				continue
			var q := (p + Vector3(rng.randf() - .5, rng.randf() - .5, rng.randf() - .5).normalized() * step).normalized() * t.radius
			sum += abs(t.sample_height(q) - h)
			n += 1
		slopes[step] = snappedf(sum / max(n, 1), 0.01)
	print(label, " mean |dh| over step: ", slopes)
func _init() -> void:
	var t: JarTerrainSdf = load("res://demo/giant/giant_terrain.tres")
	stats(t, "current")
	var hh := t.hills_height
	var oh := t.outcrops_height
	t.hills_height = 0.0
	stats(t, "no hills")
	t.outcrops_height = 0.0
	stats(t, "neither")
	quit()
