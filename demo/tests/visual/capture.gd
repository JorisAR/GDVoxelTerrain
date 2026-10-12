extends Node
## Renders a configured view and saves a screenshot, for visual checks.
## Environment: SCENE (res path), VOXEL (voxel size), MESHER (sn|dc, else the
## scene's), MANIFOLD=1, POS / LOOK ("x,y,z"), OUT (png path), WIRE=1 for
## wireframe, ON_MOON=1 (POS / LOOK relative to the moon), NOFEAT=1 (no
## population). Checks: HOLES=1 (magenta background, water / atmosphere /
## clouds hidden; prints HOLE_PIXELS), PROBE="x,y" with HOLES (what the pixel's
## ray hits, the surface there and the open edges around it), SEAMS=1 (normal
## mismatch of vertices shared by chunks), TIMING=1 (build phases). Always
## prints BUILD_MS (time until the volumes are idle).

func _ready() -> void:
	var scene: Node = load(OS.get_environment("SCENE")).instantiate()
	for v: JarVoxelVolume in scene.find_children("*", "JarVoxelVolume", true, false):
		if OS.get_environment("VOXEL") != "":
			v.voxel_size = float(OS.get_environment("VOXEL"))
		if OS.get_environment("NOFEAT") == "1":
			v.population_biome_map = null
			v.population_terrain_features = []
			v.population_terrain_details = []
		if OS.get_environment("MESHER") != "": # otherwise the scene's own settings
			v.meshing_algorithm = JarVoxelVolume.MESHING_DUAL_CONTOURING if OS.get_environment("MESHER") == "dc" else JarVoxelVolume.MESHING_SURFACE_NETS
			v.manifold = OS.get_environment("MANIFOLD") == "1"
	var player := scene.get_node_or_null("Player")
	if player:
		player.start_flying = true
		player.process_mode = Node.PROCESS_MODE_DISABLED
	add_child(scene)
	var camera: Camera3D = scene.find_children("*", "Camera3D", true, false)[0]
	var pos := _vec(OS.get_environment("POS"))
	var moon: Node3D = scene.get_node_or_null("MoonOrbit/Moon")
	if OS.get_environment("ON_MOON") != "" and moon:
		# POS / LOOK relative to the moon (it orbits, so world positions drift).
		pos += moon.global_position
	if player:
		player.global_position = pos - Vector3(0, 1.6, 0)
	camera.global_position = pos
	camera.look_at(_vec(OS.get_environment("LOOK")) + (moon.global_position if OS.get_environment("ON_MOON") != "" and moon else Vector3.ZERO))
	if OS.get_environment("HOLES") == "1":
		# Magenta background: any hole between chunks shows up as magenta.
		for env_node: WorldEnvironment in scene.find_children("*", "WorldEnvironment", true, false):
			env_node.environment.background_mode = Environment.BG_COLOR
			env_node.environment.background_color = Color(1, 0, 1)
			env_node.environment.tonemap_mode = Environment.TONE_MAPPER_LINEAR
			env_node.camera_attributes = null
			env_node.environment.fog_enabled = false
			env_node.environment.volumetric_fog_enabled = false
		# Nothing may cover the terrain: hide water, atmosphere and clouds.
		for name in ["Planet/Water", "Planet/Atmosphere", "Planet/Clouds"]:
			var n: Node3D = scene.get_node_or_null(name)
			if n:
				n.visible = false
	if OS.get_environment("WIRE") == "1":
		get_viewport().debug_draw = Viewport.DEBUG_DRAW_WIREFRAME
	# Let the volumes build around the camera.
	var volumes := scene.find_children("*", "JarVoxelVolume", true, false)
	var start := Time.get_ticks_msec()
	var idle := 0
	var phase := {}
	var frames := 0
	var last_change := start
	var last_chunks := -1
	while idle < 60 and Time.get_ticks_msec() - start < 60000:
		await get_tree().process_frame
		frames += 1
		var busy := false
		for v in volumes:
			busy = busy or v.is_building() or v.is_meshing()
			if v.is_building():
				phase["building"] = phase.get("building", 0) + 1
			elif v.is_meshing():
				phase["meshing"] = phase.get("meshing", 0) + 1
		var chunks := 0
		for c in volumes[0].get_children():
			chunks += 1 if c is JarVoxelChunk and c.visible else 0
		if chunks != last_chunks:
			last_chunks = chunks
			last_change = Time.get_ticks_msec()
		idle = 0 if busy else idle + 1
	if OS.get_environment("TIMING") == "1":
		print("PHASES frames %d %s, chunks settled at %d ms" % [frames, phase, last_change - start])
	print("BUILD_MS ", Time.get_ticks_msec() - start)
	if OS.get_environment("BOX") != "":
		# Stamp a rotated shape (SHAPE = box | cone | torus) and let it remesh.
		var shape: JarSignedDistanceField
		match OS.get_environment("SHAPE"):
			"cone":
				var cone := JarConeSdf.new()
				cone.height = 10.0
				cone.bottom_radius = 5.0
				cone.top_radius = 0.0
				shape = cone
			"torus":
				var torus := JarTorusSdf.new()
				torus.major_radius = 5.0
				torus.minor_radius = 1.6
				shape = torus
			_:
				var box := JarBoxSdf.new()
				box.extent = Vector3.ONE * 4.0
				shape = box
		var basis := Basis(Vector3.UP, 0.5) * Basis(Vector3.RIGHT, 0.35)
		var op := JarVoxelVolume.SDF_OPERATION_SUBTRACTION if OS.get_environment("DIG") == "1" else JarVoxelVolume.SDF_OPERATION_UNION
		volumes[0].edit(shape, op, Transform3D(basis, _vec(OS.get_environment("BOX"))), 0)
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
		if OS.get_environment("PROBE") != "":
			var px := OS.get_environment("PROBE").split(",")
			_probe(camera, volumes, Vector2(float(px[0]), float(px[1])))
	if OS.get_environment("SEAMS") == "1":
		_seams(volumes, camera.global_position, 200.0)
	get_tree().quit()

## Normals of vertices that neighbouring chunks share (same position): they
## must match, or the shading shows the chunk borders.
func _seams(volumes: Array, center: Vector3, radius: float) -> void:
	# position -> {chunk: [normals]}; split (crease) vertices have several.
	var at := {}
	for v: JarVoxelVolume in volumes:
		for c in v.get_children():
			if not (c is JarVoxelChunk) or not c.is_visible_in_tree():
				continue
			var mi: MeshInstance3D = c.get_mesh_instance()
			if mi == null or mi.mesh == null or (mi.global_transform * mi.mesh.get_aabb()).get_center().distance_to(center) > radius:
				continue
			for s in mi.mesh.get_surface_count():
				var arrays := mi.mesh.surface_get_arrays(s)
				var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
				var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
				for i in verts.size():
					var key := (mi.global_transform * verts[i]).snapped(Vector3.ONE * 0.001)
					if not at.has(key):
						at[key] = {}
					if not at[key].has(c):
						at[key][c] = []
					at[key][c].append((mi.global_transform.basis * normals[i]).normalized())
	# For each vertex of a chunk at a shared position: the closest normal the
	# other chunk has there.
	var buckets := {}
	var shared := 0
	for key in at:
		var chunks: Dictionary = at[key]
		if chunks.size() < 2:
			continue
		var list := chunks.keys()
		for a in range(1, list.size()):
			var same: bool = list[a].get_lod() == list[0].get_lod()
			for n: Vector3 in chunks[list[a]]:
				var best := 180.0
				for m: Vector3 in chunks[list[0]]:
					best = min(best, rad_to_deg(n.angle_to(m)))
				shared += 1
				var bucket := ("same" if same else "cross") + (" >30" if best > 30.0 else " 1-30" if best > 1.0 else " ok")
				if same and best > 1.0 and buckets.get("printed", 0) < 12:
					buckets["printed"] = buckets.get("printed", 0) + 1
					print("SEAMS example %s best %.1f  A(%s lod %d): %s  B(%s lod %d): %s" % [key, best, list[0].name, list[0].get_lod(), chunks[list[0]], list[a].name, list[a].get_lod(), chunks[list[a]]])
				buckets[bucket] = buckets.get(bucket, 0) + 1
	print("SEAMS shared %d %s" % [shared, buckets])

func _vec(s: String) -> Vector3:
	var p := s.split(",")
	return Vector3(float(p[0]), float(p[1]), float(p[2]))

## Lists the chunk triangles along the camera ray through a pixel, nearest
## first: which chunk and LOD, whether the triangle faces the camera, and how
## its face normal compares with its vertex normals.
func _probe(camera: Camera3D, volumes: Array, pixel: Vector2) -> void:
	var from := camera.project_ray_origin(pixel)
	var dir := camera.project_ray_normal(pixel)
	var hits := []
	for v: JarVoxelVolume in volumes:
		for c in v.get_children():
			if not (c is JarVoxelChunk) or not c.visible:
				continue
			var mi: MeshInstance3D = c.get_mesh_instance()
			if mi == null or mi.mesh == null or not mi.is_visible_in_tree():
				continue
			var xf := mi.global_transform
			var inv := xf.affine_inverse()
			var lf := inv * from
			var ld := (inv.basis * dir).normalized()
			if not mi.mesh.get_aabb().grow(1.0).intersects_ray(lf, ld):
				continue
			for s in mi.mesh.get_surface_count():
				var arrays := mi.mesh.surface_get_arrays(s)
				var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
				var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
				var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
				for t in range(0, idx.size(), 3):
					var a := verts[idx[t]]; var b := verts[idx[t + 1]]; var cc := verts[idx[t + 2]]
					var hit = Geometry3D.ray_intersects_triangle(lf, ld, a, b, cc)
					if hit == null:
						continue
					var face := (cc - a).cross(b - a).normalized()
					var nsum := normals[idx[t]] + normals[idx[t + 1]] + normals[idx[t + 2]]
					hits.append([lf.distance_to(hit), c.name, c.get_lod(), face.dot(ld) < 0.0, face.dot(nsum.normalized()), xf * a, xf * b, xf * cc, normals[idx[t]], normals[idx[t + 1]], normals[idx[t + 2]]])
	# Where the surface is along the ray (the SDF), and the chunks around it.
	var vol: JarVoxelVolume = volumes[0]
	var sdf: JarSignedDistanceField = vol.sdf
	var vinv := vol.global_transform.affine_inverse()
	var prev := sdf.sample(vinv * from)
	var t := 0.0
	while t < 4000.0:
		t += 0.25
		var d := sdf.sample(vinv * (from + dir * t))
		if sign(d) != sign(prev):
			break
		prev = d
	var surface := from + dir * t
	print("PROBE surface at d=%.2f %s" % [t, surface])
	for c in vol.get_children():
		if not (c is JarVoxelChunk):
			continue
		var mi: MeshInstance3D = c.get_mesh_instance()
		if mi == null or mi.mesh == null:
			continue
		var box: AABB = mi.global_transform * mi.mesh.get_aabb()
		if not box.grow(2.0).has_point(surface):
			continue
		var best := 1e9
		for s in mi.mesh.get_surface_count():
			for vtx in mi.mesh.surface_get_arrays(s)[Mesh.ARRAY_VERTEX]:
				best = min(best, (mi.global_transform * vtx).distance_to(surface))
		print("PROBE   chunk %s lod=%d visible=%s pos=%s aabb=%s nearest vertex %.2f" % [c.name, c.get_lod(), c.is_visible_in_tree(), c.global_position, box, best])
	# Open edges near the surface point, over all chunks (vertices welded).
	var edges := {}
	var owner := {}
	for c in vol.get_children():
		if not (c is JarVoxelChunk) or not c.is_visible_in_tree():
			continue
		var mi: MeshInstance3D = c.get_mesh_instance()
		if mi == null or mi.mesh == null or not (mi.global_transform * mi.mesh.get_aabb()).grow(6.0).has_point(surface):
			continue
		for s in mi.mesh.get_surface_count():
			var arrays := mi.mesh.surface_get_arrays(s)
			var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
			var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
			for ti in range(0, idx.size(), 3):
				var tri := [mi.global_transform * verts[idx[ti]], mi.global_transform * verts[idx[ti + 1]], mi.global_transform * verts[idx[ti + 2]]]
				if (tri[0] + tri[1] + tri[2]).distance_to(surface * 3.0) > 3.0 * 6.0:
					continue
				for k in 3:
					var ea: Vector3 = tri[k].snapped(Vector3.ONE * 0.001)
					var eb: Vector3 = tri[(k + 1) % 3].snapped(Vector3.ONE * 0.001)
					var key := [ea, eb] if str(ea) < str(eb) else [eb, ea]
					edges[key] = edges.get(key, 0) + 1
					owner[key] = "%s(lod %d)" % [c.name, c.get_lod()]
	for key in edges:
		if edges[key] == 1 and (key[0] + key[1]).distance_to(surface * 2.0) < 2.0 * 4.0:
			print("PROBE open edge %s - %s  in %s" % [key[0], key[1], owner[key]])
	hits.sort_custom(func(x, y): return x[0] < y[0])
	for h in hits.slice(0, 6):
		print("PROBE d=%.2f chunk=%s lod=%d front=%s face.n=%.2f  tri=%s %s %s  normals=%s %s %s" % h)
