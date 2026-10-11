extends Node
## Renders a configured view and saves a screenshot, for visual checks.
## Environment: SCENE (res path), VOXEL (voxel size), MESHER (sn|dc),
## POS / LOOK ("x,y,z"), OUT (png path), WIRE=1 for wireframe.

func _ready() -> void:
	var scene: Node = load(OS.get_environment("SCENE")).instantiate()
	for v: JarVoxelVolume in scene.find_children("*", "JarVoxelVolume", true, false):
		if OS.get_environment("VOXEL") != "":
			v.voxel_size = float(OS.get_environment("VOXEL"))
		v.meshing_algorithm = JarVoxelVolume.MESHING_DUAL_CONTOURING if OS.get_environment("MESHER") == "dc" else JarVoxelVolume.MESHING_SURFACE_NETS
	var player := scene.get_node_or_null("Player")
	if player:
		player.start_flying = true
		player.process_mode = Node.PROCESS_MODE_DISABLED
	add_child(scene)
	var camera: Camera3D = scene.find_children("*", "Camera3D", true, false)[0]
	var pos := _vec(OS.get_environment("POS"))
	if player:
		player.global_position = pos - Vector3(0, 1.6, 0)
	camera.global_position = pos
	camera.look_at(_vec(OS.get_environment("LOOK")))
	if OS.get_environment("HOLES") == "1":
		# Magenta background: any hole between chunks shows up as magenta.
		for env_node: WorldEnvironment in scene.find_children("*", "WorldEnvironment", true, false):
			env_node.environment.background_mode = Environment.BG_COLOR
			env_node.environment.background_color = Color(1, 0, 1)
			env_node.environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
	if OS.get_environment("WIRE") == "1":
		get_viewport().debug_draw = Viewport.DEBUG_DRAW_WIREFRAME
	# Let the volumes build around the camera.
	var volumes := scene.find_children("*", "JarVoxelVolume", true, false)
	var start := Time.get_ticks_msec()
	var idle := 0
	while idle < 60 and Time.get_ticks_msec() - start < 60000:
		await get_tree().process_frame
		var busy := false
		for v in volumes:
			busy = busy or v.is_building() or v.is_meshing()
		idle = 0 if busy else idle + 1
	if OS.get_environment("BOX") != "":
		# Stamp a rotated box (like a user brush) and let it remesh.
		var box := JarBoxSdf.new()
		box.extent = Vector3.ONE * 4.0
		var basis := Basis(Vector3.UP, 0.5) * Basis(Vector3.RIGHT, 0.35)
		volumes[0].edit(box, JarVoxelVolume.SDF_OPERATION_UNION, Transform3D(basis, _vec(OS.get_environment("BOX"))), 0)
		idle = 0
		while idle < 60:
			await get_tree().process_frame
			idle = 0 if volumes[0].is_building() or volumes[0].is_meshing() else idle + 1
	if OS.get_environment("PREVIEW") != "":
		# The brush preview ghost (sphere, radius 3) centered at PREVIEW.
		var ghost := MeshInstance3D.new()
		var sphere := SphereMesh.new()
		sphere.radius = 3.0
		sphere.height = 6.0
		ghost.mesh = sphere
		var mat := ShaderMaterial.new()
		mat.shader = load("res://demo/editing/brush_preview.gdshader")
		ghost.material_override = mat
		add_child(ghost)
		ghost.global_position = _vec(OS.get_environment("PREVIEW"))
		for i in 5:
			await get_tree().process_frame
	await RenderingServer.frame_post_draw
	var image := get_viewport().get_texture().get_image()
	image.save_png(OS.get_environment("OUT"))
	if OS.get_environment("HOLES") == "1":
		var holes := 0
		for y in image.get_height():
			for x in image.get_width():
				var c := image.get_pixel(x, y)
				if c.r > 0.9 and c.b > 0.9 and c.g < 0.1:
					holes += 1
		var chunks := 0
		for v: JarVoxelVolume in volumes:
			for c in v.get_children():
				chunks += 1 if c is JarVoxelChunk and c.visible else 0
		print("HOLE_PIXELS ", holes, " voxel=", volumes[0].voxel_size, " chunks=", chunks)
	get_tree().quit()

func _vec(s: String) -> Vector3:
	var p := s.split(",")
	return Vector3(float(p[0]), float(p[1]), float(p[2]))
