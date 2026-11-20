extends Node
func _ready() -> void:
	var body := JarCelestialBody.new()
	body.radius = 600000.0
	add_child(body)
	var clouds := JarBodyClouds.new()
	body.add_child(clouds)
	var t := Time.get_ticks_msec()
	await get_tree().process_frame
	await get_tree().process_frame
	print("frames ms ", Time.get_ticks_msec() - t)
	var w = clouds.get_weather()
	print("weather ", w, " param ", clouds.get_active_material().get_shader_parameter("weather_map"))
	if w is Cubemap:
		var c := w as Cubemap
		print("size ", c.get_width(), " layers ", c.get_layers(), " mip ", c.has_mipmaps())
		var img := c.get_layer_data(0)
		if img:
			var s := 0.0
			for i in 100:
				s += img.get_pixel(i * 5, 256).r
			print("face0 row mean r ", s / 100.0, " g ", img.get_pixel(256, 256).g, " b ", img.get_pixel(256, 256).b)
	get_tree().quit()
