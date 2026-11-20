extends Node
func _ready() -> void:
	var shader: Shader = load("res://addons/jar_voxel_terrain/nodes/clouds/clouds.gdshader")
	print("uniforms ", shader.get_shader_uniform_list().size())
	var body := JarCelestialBody.new()
	body.radius = 1000.0
	add_child(body)
	var t := Time.get_ticks_usec()
	var clouds := JarBodyClouds.new()
	body.add_child(clouds)
	print("clouds ready in ", (Time.get_ticks_usec() - t) / 1000.0, " ms")
	await get_tree().process_frame
	await get_tree().process_frame
	var tex: Texture3D = clouds.get_active_material().get_shader_parameter("cloud_noise")
	print("noise ", tex, " ", tex.get_width() if tex else -1, " ", tex.get_depth() if tex else -1, " mips ", tex.has_mipmaps() if tex else false)
	var img: Image = tex.get_data()[10]
	var sum := Color()
	for i in 64:
		sum += img.get_pixel(i, 20)
	print("avg row ", sum / 64.0)
	get_tree().quit()
