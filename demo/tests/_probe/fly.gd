extends SceneTree
func _init() -> void:
	var m = load("res://demo/giant/giant_biomes.tres")
	print(m.get_method_list().map(func(x): return x.name).filter(func(n): return "biome" in n or "weight" in n or "sample" in n))
	quit()
