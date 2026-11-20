extends Node

func _ready() -> void:
	for path in ["res://demo/planet/planet_terrain.tres", "res://demo/giant/giant_terrain.tres"]:
		var sdf: JarTerrainSdf = load(path)
		var rng := RandomNumberGenerator.new()
		rng.seed = 11
		var out := PackedStringArray()
		for i in 3000:
			var dir := Vector3(rng.randf_range(-1, 1), rng.randf_range(-1, 1), rng.randf_range(-1, 1)).normalized()
			var off := rng.randf_range(-1.5, 1.5)
			var p := dir * (sdf.radius + sdf.sample_height(dir * sdf.radius) + off)
			var n := sdf.sample_normal(p)
			out.append("%.6f %.6f %.6f %.3f" % [n.x, n.y, n.z, off])
		var t := Time.get_ticks_usec()
		for i in 3000:
			sdf.sample_normal(Vector3(sdf.radius, 0, 0) + Vector3(i * 0.37, i * 0.11, 0))
		printerr("NORMALS ", path.get_file(), " us per normal %.1f" % ((Time.get_ticks_usec() - t) / 3000.0))
		FileAccess.open(OS.get_environment("OUT") + "_" + path.get_file() + ".txt", FileAccess.WRITE).store_string("\n".join(out))
	get_tree().quit()
