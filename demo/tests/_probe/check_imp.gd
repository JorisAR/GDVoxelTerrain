extends Node
func _ready() -> void:
	for n in ["birch", "pine"]:
		var q: QuadMesh = ResourceLoader.load("res://demo/planet/%s_impostor.res" % n, "", ResourceLoader.CACHE_MODE_IGNORE)
		print(n, " ", q.material.get_shader_parameter("variation"), " ", q.material.get_shader_parameter("variation_a"))
	get_tree().quit()
