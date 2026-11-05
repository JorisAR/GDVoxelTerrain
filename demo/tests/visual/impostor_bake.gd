extends Node3D
## Bakes TREE (scene path) with JarImpostorBaker and compares the mesh (left),
## the new impostor (middle) and the scene's old one (right).
## Environment: TREE, OUT (png prefix). Bakes a sphere first and prints its
## normals, to prove their encoding.

func _ready() -> void:
	var sphere := PackedScene.new()
	var m := MeshInstance3D.new()
	m.name = "S"
	m.mesh = SphereMesh.new()
	sphere.pack(m)
	var baker := JarImpostorBaker.new()
	baker.frames = 4
	var probe: QuadMesh = await baker.bake(sphere, self)
	var img: Image = probe.material.get_shader_parameter("imposterTextureNormal").get_image()
	var s := baker.frame_size
	print("center ", img.get_pixel(s / 2, s / 2), " right ", img.get_pixel(s - s / 4, s / 2), " top ", img.get_pixel(s / 2, s / 4))
	baker = JarImpostorBaker.new()
	var scene: PackedScene = load(OS.get_environment("TREE"))
	var start := Time.get_ticks_msec()
	var quad: QuadMesh = await baker.bake(scene, self)
	print("baked in ", Time.get_ticks_msec() - start, " ms")
	var mesh_tree: Node3D = scene.instantiate()
	var new_tree := MeshInstance3D.new()
	new_tree.mesh = quad
	var old_tree: Node3D = scene.instantiate()
	for t in [mesh_tree, new_tree, old_tree]:
		add_child(t)
	var size := quad.size.x
	mesh_tree.position.x = -size * 0.35
	old_tree.position.x = size * 0.35
	var lods := JarImpostorBaker._source_meshes(mesh_tree)
	for n: GeometryInstance3D in mesh_tree.find_children("*", "GeometryInstance3D", true, false):
		n.visible = n in lods
		n.visibility_range_end = 0.0
	for n: GeometryInstance3D in old_tree.find_children("*", "GeometryInstance3D", true, false):
		n.visible = n.name == "mesh-impostor"
		n.visibility_range_begin = 0.0
		n.visibility_range_end = 0.0
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
	var center: Vector3 = quad.center_offset
	var views := [[0.0, 5.0], [130.0, 25.0], [250.0, 55.0]]
	var out := OS.get_environment("OUT")
	for i in views.size():
		var yaw := deg_to_rad(views[i][0])
		var pitch := deg_to_rad(views[i][1])
		for t: Node3D in [mesh_tree, new_tree, old_tree]:
			t.rotation.y = -yaw
		camera.position = center + Vector3(0, sin(pitch), cos(pitch)) * size * 1.0
		camera.look_at(center)
		for f in 4:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png("%s_%d.png" % [out, i])
	quad.material.get_shader_parameter("imposterTextureAlbedo").get_image().save_png(out + "_albedo.png")
	quad.material.get_shader_parameter("imposterTextureNormal").get_image().save_png(out + "_normal.png")
	get_tree().quit()
