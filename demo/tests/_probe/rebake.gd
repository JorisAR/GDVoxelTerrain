extends Node
func _ready() -> void:
	for n in ["birch", "broadleaf", "broadleaf_large", "broadleaf_savanna", "bush", "pine", "pine_sparse"]:
		var feature: JarTerrainFeature = load("res://demo/planet/%s.tres" % n)
		var baker := JarImpostorBaker.new()
		var t := Time.get_ticks_msec()
		var err: Error = await baker.bake_feature(feature, self)
		var mat: ShaderMaterial = (feature.impostor as QuadMesh).material
		print(n, " err ", err, " ms ", Time.get_ticks_msec() - t, " variation ", mat.get_shader_parameter("variation"), " ", mat.get_shader_parameter("variation_a"), " ", mat.get_shader_parameter("variation_b"))
	get_tree().quit()
