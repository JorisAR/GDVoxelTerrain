extends SceneTree
## Terrain height percentiles of a JarTerrainSdf (SDF=res path).
func _init() -> void:
	var sdf: JarTerrainSdf = load(OS.get_environment("SDF"))
	var rng := RandomNumberGenerator.new()
	rng.seed = 3
	var hs: Array[float] = []
	for i in 20000:
		var d := Vector3(rng.randfn(), rng.randfn(), rng.randfn()).normalized()
		hs.append(sdf.sample_height(d * sdf.radius))
	hs.sort()
	var land := hs.filter(func(h): return h > 0.0)
	print("radius ", sdf.radius, " land ", land.size() / 200.0, "%")
	for q in [0.5, 0.9, 0.99, 0.999, 1.0]:
		print("all p", q, " ", hs[mini(int(q * hs.size()), hs.size() - 1)], "  land p", q, " ", land[mini(int(q * land.size()), land.size() - 1)])
	quit()
