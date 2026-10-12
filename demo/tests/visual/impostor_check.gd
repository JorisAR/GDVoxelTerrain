extends Node3D
## Shows a feature scene's mesh LOD (left) next to its impostor (right) from
## a few directions, for checking impostor bakes and the impostor shader.
## Environment: TREE (scene path), OUT (png prefix), LOD (mesh node, LoD1).

func _ready() -> void:
	var scene: PackedScene = load(OS.get_environment("TREE"))
	var lod := OS.get_environment("LOD") if OS.get_environment("LOD") != "" else "LoD1"
	var mesh_tree := scene.instantiate()
	var impostor_tree := scene.instantiate()
	add_child(mesh_tree)
	add_child(impostor_tree)
	var size := 0.0
	for n: GeometryInstance3D in mesh_tree.find_children("*", "GeometryInstance3D", true, false):
		n.visible = n.name == lod
		n.visibility_range_begin = 0.0
		n.visibility_range_end = 0.0
		if n.visible:
			size = max(size, n.get_aabb().size.length())
	for n: GeometryInstance3D in impostor_tree.find_children("*", "GeometryInstance3D", true, false):
		n.visible = n.name == "mesh-impostor"
		n.visibility_range_begin = 0.0
		n.visibility_range_end = 0.0
	mesh_tree.position = Vector3(-size * 0.4, 0, 0)
	impostor_tree.position = Vector3(size * 0.4, 0, 0)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-50, 30, 0)
	add_child(light)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.background_mode = Environment.BG_COLOR
	env.environment.background_color = Color(0.5, 0.6, 0.75)
	env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.environment.ambient_light_color = Color(0.45, 0.5, 0.6)
	add_child(env)
	var camera := Camera3D.new()
	add_child(camera)
	camera.current = true
	var center := Vector3(0, size * 0.3, 0)
	var views := [[0.0, 5.0], [90.0, 5.0], [200.0, 30.0], [300.0, 60.0]]
	for i in views.size():
		var yaw := deg_to_rad(views[i][0])
		var pitch := deg_to_rad(views[i][1])
		camera.position = center + Vector3(sin(yaw) * cos(pitch), sin(pitch), cos(yaw) * cos(pitch)) * size * 1.6
		camera.look_at(center)
		# Rotate the trees instead of the camera sideways: both seen alike.
		mesh_tree.rotation.y = -yaw
		impostor_tree.rotation.y = -yaw
		camera.position = center + Vector3(0, sin(pitch), cos(pitch)) * size * 1.6
		camera.look_at(center)
		for f in 4:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_%d.png" % [OS.get_environment("OUT"), i])
	get_tree().quit()
