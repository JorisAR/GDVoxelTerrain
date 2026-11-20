extends Node
## Accuracy of the meshers on a box rotated off the grid (the experiment of
## "Dual Contouring of Signed Distance Data", Carrera et al. 2026): distance of
## the mesh from the true surface, and of the true sharp edges and corners
## from the mesh. MODE=edit (a stamped box: samples with exact normals) or
## MODE=field (the volume's own SDF: gradients by finite differences).

var volume: JarVoxelVolume
const HALF := 6.0
var box_basis := Basis.from_euler(Vector3(0.5, 0.7, 0.3))
var box_center := Vector3(0.3, 40.4, 0.2)

func _ready() -> void:
	var mode := OS.get_environment("MODE")
	var viewer := JarVoxelViewer.new()
	if mode == "field":
		volume = JarVoxelVolume.new()
		volume.chunk_scene = TestWorld.CHUNK_SCENE
		var source := BoxMesh.new()
		source.size = Vector3.ONE * HALF * 2.0
		var arrays := source.get_mesh_arrays()
		var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
		box_center = Vector3(0.3, 0.4, 0.2)
		for i in verts.size():
			verts[i] = box_basis * verts[i] + box_center
			normals[i] = box_basis * normals[i]
		arrays[Mesh.ARRAY_VERTEX] = verts
		arrays[Mesh.ARRAY_NORMAL] = normals
		var rotated := ArrayMesh.new()
		rotated.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
		var sdf := JarMeshSdf.new()
		sdf.mesh = rotated
		volume.sdf = sdf
		volume.extent = 64.0
		add_child(volume)
		viewer.position = box_center + Vector3(0, HALF + 3.0, 0)
		add_child(viewer)
	else:
		var world := TestWorld.flat(self, Vector3(0, 10, 0))
		volume = world.volume
		viewer = world.viewer
		await _settle()
		viewer.position = box_center + Vector3(0, HALF + 3.0, 0)
		volume.force_update_lod()
		await _settle()
		var box := JarBoxSdf.new()
		box.extent = Vector3.ONE * HALF
		volume.edit(box, JarVoxelVolume.SDF_OPERATION_UNION, Transform3D(box_basis, box_center))
	await _settle()
	for algorithm in [JarVoxelVolume.MESHING_SURFACE_NETS, JarVoxelVolume.MESHING_DUAL_CONTOURING]:
		volume.meshing_algorithm = algorithm
		volume.manifold = OS.get_environment("MANIFOLD") == "1"
		await _settle()
		_measure("surface nets" if algorithm == JarVoxelVolume.MESHING_SURFACE_NETS else "dual contouring")
		for i in 120:
			await get_tree().physics_frame
		_measure("again")
	if OS.get_environment("OUT") != "":
		var cam := Camera3D.new()
		add_child(cam)
		cam.fov = 40.0
		var light := DirectionalLight3D.new()
		add_child(light)
		light.look_at_from_position(Vector3(4, 9, 6), Vector3.ZERO)
		var env := WorldEnvironment.new()
		env.environment = Environment.new()
		env.environment.background_mode = Environment.BG_COLOR
		env.environment.background_color = Color(0.35, 0.4, 0.5)
		env.environment.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
		env.environment.ambient_light_color = Color(0.5, 0.5, 0.55)
		add_child(env)
		cam.look_at_from_position(box_center + Vector3(14, 12, 18), box_center)
		cam.make_current()
		if OS.get_environment("WIRE") == "1":
			get_viewport().debug_draw = Viewport.DEBUG_DRAW_WIREFRAME
		for i in 20:
			await get_tree().process_frame
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(OS.get_environment("OUT"))
	get_tree().quit()

func _settle() -> void:
	var idle := 0
	while idle < 30:
		await get_tree().physics_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0

## Signed distance to the true box.
func _box(p: Vector3) -> float:
	var q := (box_basis.inverse() * (p - box_center)).abs() - Vector3.ONE * HALF
	return Vector3(maxf(q.x, 0), maxf(q.y, 0), maxf(q.z, 0)).length() + minf(maxf(q.x, maxf(q.y, q.z)), 0.0)

func _point_triangle(p: Vector3, a: Vector3, b: Vector3, c: Vector3) -> float:
	var ab := b - a
	var ac := c - a
	var ap := p - a
	var d1 := ab.dot(ap)
	var d2 := ac.dot(ap)
	if d1 <= 0.0 and d2 <= 0.0:
		return ap.length()
	var bp := p - b
	var d3 := ab.dot(bp)
	var d4 := ac.dot(bp)
	if d3 >= 0.0 and d4 <= d3:
		return bp.length()
	var vc := d1 * d4 - d3 * d2
	if vc <= 0.0 and d1 >= 0.0 and d3 <= 0.0:
		return (ap - ab * (d1 / (d1 - d3))).length()
	var cp := p - c
	var d5 := ab.dot(cp)
	var d6 := ac.dot(cp)
	if d6 >= 0.0 and d5 <= d6:
		return cp.length()
	var vb := d5 * d2 - d1 * d6
	if vb <= 0.0 and d2 >= 0.0 and d6 <= 0.0:
		return (ap - ac * (d2 / (d2 - d6))).length()
	var va := d3 * d6 - d5 * d4
	if va <= 0.0 and (d4 - d3) >= 0.0 and (d5 - d6) >= 0.0:
		return (bp - (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)))).length()
	var denom := 1.0 / (va + vb + vc)
	return (ap - ab * (vb * denom) - ac * (vc * denom)).length()

func _measure(label: String) -> void:
	var tris: Array[Vector3] = []
	for c in volume.get_children():
		if not (c is JarVoxelChunk and c.visible):
			continue
		var mesh: Mesh = c.get_mesh()
		if mesh == null or mesh.get_surface_count() == 0:
			continue
		var arrays := mesh.surface_get_arrays(0)
		var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
		for t in range(0, indices.size(), 3):
			var a: Vector3 = c.position + verts[indices[t]]
			var b: Vector3 = c.position + verts[indices[t + 1]]
			var d: Vector3 = c.position + verts[indices[t + 2]]
			# Only the box's own surface (the ground is far below).
			if absf(_box((a + b + d) / 3.0)) < 2.0:
				tris.append(a)
				tris.append(b)
				tris.append(d)
	# Mesh -> truth: vertices, edge midpoints and centers of every triangle.
	var sum := 0.0
	var worst := 0.0
	var count := 0
	for t in range(0, tris.size(), 3):
		var a := tris[t]
		var b := tris[t + 1]
		var c := tris[t + 2]
		for p: Vector3 in [a, b, c, (a + b) * 0.5, (b + c) * 0.5, (c + a) * 0.5, (a + b + c) / 3.0]:
			var e := absf(_box(p))
			sum += e
			worst = maxf(worst, e)
			count += 1
	# Triangles facing into the box (flipped): against the true outward direction.
	var flipped := 0
	for t in range(0, tris.size(), 3):
		var center := (tris[t] + tris[t + 1] + tris[t + 2]) / 3.0
		var e := 0.05
		var outward := Vector3(_box(center + Vector3(e, 0, 0)) - _box(center - Vector3(e, 0, 0)), _box(center + Vector3(0, e, 0)) - _box(center - Vector3(0, e, 0)), _box(center + Vector3(0, 0, e)) - _box(center - Vector3(0, 0, e)))
		var normal := (tris[t + 2] - tris[t]).cross(tris[t + 1] - tris[t])
		if normal.length() > 1e-6 and normal.normalized().dot(outward.normalized()) < -0.2:
			flipped += 1
	# Truth -> mesh at the sharp features: points along the 12 edges, the 8 corners.
	var feature_sum := 0.0
	var feature_worst := 0.0
	var feature_count := 0
	var corner_worst := 0.0
	var samples: Array[Vector3] = []
	for axis in 3:
		for s1: float in [-1.0, 1.0]:
			for s2: float in [-1.0, 1.0]:
				for k in 13:
					var local := Vector3()
					local[axis] = lerpf(-HALF, HALF, k / 12.0)
					local[(axis + 1) % 3] = s1 * HALF
					local[(axis + 2) % 3] = s2 * HALF
					samples.append(box_basis * local + box_center)
	for p in samples:
		var best := INF
		for t in range(0, tris.size(), 3):
			if tris[t].distance_squared_to(p) > 400.0:
				continue
			if (tris[t + 1] - tris[t]).cross(tris[t + 2] - tris[t]).length_squared() < 1e-10:
				continue # degenerate: no area to be near
			var dist := _point_triangle(p, tris[t], tris[t + 1], tris[t + 2])
			if not is_nan(dist):
				best = minf(best, dist)
		feature_sum += best
		feature_worst = maxf(feature_worst, best)
		feature_count += 1
		var local := box_basis.inverse() * (p - box_center)
		if absf(absf(local.x) - HALF) < 1e-3 and absf(absf(local.y) - HALF) < 1e-3 and absf(absf(local.z) - HALF) < 1e-3:
			corner_worst = maxf(corner_worst, best)
	print("RESULT %-16s triangles %5d | mesh to surface: mean %.4f max %.4f | sharp edges to mesh: mean %.4f max %.4f | corners max %.4f | flipped %d  (voxels)" % [label, tris.size() / 3, sum / maxi(count, 1), worst, feature_sum / feature_count, feature_worst, corner_worst, flipped])
