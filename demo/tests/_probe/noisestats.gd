extends Node
func _ready() -> void:
	var body := JarCelestialBody.new()
	body.radius = 1000.0
	add_child(body)
	var clouds := JarBodyClouds.new()
	body.add_child(clouds)
	await get_tree().process_frame
	await get_tree().process_frame
	var tex: Texture3D = clouds.get_active_material().get_shader_parameter("cloud_noise")
	var imgs := tex.get_data()
	for ch in 4:
		var vals := PackedFloat32Array()
		for z in range(0, 64, 4):
			var img: Image = imgs[z]
			for y in range(0, 64, 2):
				for x in range(0, 64, 2):
					vals.append(img.get_pixel(x, y)[ch])
		vals.sort()
		var n := vals.size()
		var mean := 0.0
		for v in vals:
			mean += v
		print("ch ", ch, " mean ", mean / n, " p10 ", vals[n / 10], " p50 ", vals[n / 2], " p70 ", vals[n * 7 / 10], " p90 ", vals[n * 9 / 10], " max ", vals[n - 1])
	get_tree().quit()
