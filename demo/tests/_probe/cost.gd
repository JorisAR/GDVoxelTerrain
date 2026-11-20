extends SceneTree
func run(t: JarTerrainSdf) -> float:
	var rng := RandomNumberGenerator.new()
	rng.seed = 3
	var ps: Array[Vector3] = []
	for i in 20000:
		ps.append(Vector3(rng.randfn(), rng.randfn(), rng.randfn()).normalized() * t.radius)
	var s := Time.get_ticks_usec()
	for p in ps:
		t.sample_height(p)
	return (Time.get_ticks_usec() - s) / 20000.0
func _init() -> void:
	var t: JarTerrainSdf = load("res://demo/giant/giant_terrain.tres")
	run(t)
	print("with ", run(t), " us")
	t.hills_height = 0.0
	t.outcrops_height = 0.0
	run(t)
	print("without ", run(t), " us")
	quit()
