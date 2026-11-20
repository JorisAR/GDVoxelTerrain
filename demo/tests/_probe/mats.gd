extends Node
func _ready() -> void:
	var feature: JarTerrainFeature = load("res://demo/planet/birch.tres")
	var src: Node3D = JarImpostorBaker._feature_source(feature)
	add_child(src)
	for m in JarImpostorBaker._source_meshes(src):
		print(m.name, " override ", m.material_override, " surfaces ", m.mesh.get_surface_count() if m.mesh else -1, " overlay ", m.material_overlay)
		if m.mesh:
			for i in m.mesh.get_surface_count():
				var mat := m.get_active_material(i)
				print("  surface ", i, " ", mat, " ", (mat as ShaderMaterial).shader.resource_path if mat is ShaderMaterial else "")
				if mat is ShaderMaterial:
					print("    c1 ", mat.get_shader_parameter("leaves_color1"))
	get_tree().quit()
