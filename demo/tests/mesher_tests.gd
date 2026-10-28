extends Node
## Headless checks for the meshers: stamps sharp shapes, meshes them with
## surface nets, switches the volume to dual contouring at runtime and checks
## that sharp features are reproduced exactly. Run:
##   godot --headless --path demo res://tests/mesher_tests.tscn

var _failures := 0
var volume: JarVoxelVolume

func _ready() -> void:
	var world := TestWorld.flat(self, Vector3(0, 10, 0))
	var camera: Node3D = world.viewer
	volume = world.volume
	await _settle()

	var ground := _ray_height(Vector3(0.3, 0, 0.3))
	camera.position = Vector3(0, ground + 8, 0) # full detail at the stamps
	volume.force_update_lod()
	await _settle()

	# A 4 m prism (ridge along Z at x = 0.3) and a box with corners off the voxel grid.
	var prism := JarMeshSdf.new()
	prism.mesh = PrismMesh.new()
	prism.mesh.size = Vector3(2, 2, 2)
	volume.edit(prism, JarVoxelVolume.SDF_OPERATION_UNION, Transform3D(Basis().scaled(Vector3.ONE * 2.0), Vector3(0.3, ground + 2.0, 0.3)))
	var box := JarBoxSdf.new()
	box.extent = Vector3(1.7, 1.7, 1.7)
	var box_center := Vector3(8.3, ground + 1.0, 0.3)
	volume.edit(box, JarVoxelVolume.SDF_OPERATION_UNION, Transform3D(Basis(), box_center))
	await _settle()

	var apex := ground + 4.0
	var box_top := box_center.y + 1.7
	var sn_peak := _ray_height(Vector3(0.3, 0, 0.3))
	print("INFO surface nets: prism peak %.3f of %.3f, box corner vertex error %.3f" % [sn_peak, apex, _corner_error(box_center + Vector3(1.7, 1.7, 1.7))])

	volume.meshing_algorithm = JarVoxelVolume.MESHING_DUAL_CONTOURING
	await _settle()
	var dc_peak := _ray_height(Vector3(0.3, 0, 0.3))
	_check("dc prism apex exact", absf(dc_peak - apex) < 0.02, "peak %.3f of %.3f" % [dc_peak, apex])
	_check("dc box top flat", absf(_ray_height(box_center + Vector3(1.6, 0, 1.6)) - box_top) < 0.02)
	var corner_err := _corner_error(box_center + Vector3(1.7, 1.7, 1.7))
	_check("dc box corner vertex", corner_err < 0.02, "error %.3f" % corner_err)
	_check("dc no spikes above apex", _highest_vertex_near(Vector3(0.3, 0, 0.3), 3.0) < apex + 0.02)

	# Region meshes (voxel blocks through the chunk mesher, in tiles): a ball
	# floating above the ground, spanning several tiles, meshes closed.
	var ball := JarSphereSdf.new()
	ball.radius = 9.6
	var ball_center := Vector3(20.3, ground + 40.0, 0.7)
	camera.position = ball_center + Vector3(0, 12, 0)
	volume.force_update_lod()
	volume.edit(ball, JarVoxelVolume.SDF_OPERATION_UNION, Transform3D(Basis(), ball_center))
	await _settle()
	var region := volume.get_region_mesh(AABB(ball_center - Vector3.ONE * 11.0, Vector3.ONE * 22.0))
	_check("region mesh made", region != null and region.get_surface_count() == 1)
	if region:
		var r := _closed_area(region)
		var expected := 4.0 * PI * ball.radius * ball.radius
		_check("region mesh closed", r.open == 0, "%d open edges" % r.open)
		_check("region mesh area", absf(r.area - expected) < 0.05 * expected, "%.0f of %.0f m2" % [r.area, expected])

	print("Mesher tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

## Open edges (welded at 1 mm) and total area of a mesh.
func _closed_area(mesh: Mesh) -> Dictionary:
	var arrays := mesh.surface_get_arrays(0)
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	var key := func(v: Vector3) -> Vector3i: return Vector3i((v * 1000.0).round())
	var edges := {}
	var area := 0.0
	for t in range(0, indices.size(), 3):
		var a := verts[indices[t]]
		var b := verts[indices[t + 1]]
		var c := verts[indices[t + 2]]
		area += (b - a).cross(c - a).length() * 0.5
		for e in [[a, b], [b, c], [c, a]]:
			var ka: Vector3i = key.call(e[0])
			var kb: Vector3i = key.call(e[1])
			if ka == kb:
				continue
			var k := [ka, kb] if str(ka) < str(kb) else [kb, ka]
			edges[k] = edges.get(k, 0) + 1
	var open := 0
	for k in edges:
		if edges[k] == 1:
			open += 1
	return {open = open, area = area}

func _check(name: String, ok: bool, detail := "") -> void:
	if not ok:
		_failures += 1
	print("%s %s %s" % ["PASS" if ok else "FAIL", name, detail])

func _settle() -> void:
	var idle := 0
	while idle < 30:
		await get_tree().physics_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0

func _ray_height(at: Vector3) -> float:
	var hit := volume.get_world_3d().direct_space_state.intersect_ray(
		PhysicsRayQueryParameters3D.create(Vector3(at.x, 500, at.z), Vector3(at.x, -500, at.z)))
	return hit.position.y if hit else NAN

func _vertices() -> PackedVector3Array:
	var out := PackedVector3Array()
	for c in volume.get_children():
		if not (c is JarVoxelChunk and c.visible):
			continue
		var mesh: Mesh = c.get_mesh_instance().mesh
		if mesh.get_surface_count() == 0:
			continue
		for v in mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]:
			out.append(c.position + v)
	return out

## Distance from `corner` to the closest mesh vertex.
func _corner_error(corner: Vector3) -> float:
	var best := INF
	for v in _vertices():
		best = minf(best, v.distance_to(corner))
	return best

func _highest_vertex_near(at: Vector3, radius: float) -> float:
	var best := -INF
	for v in _vertices():
		if Vector2(v.x - at.x, v.z - at.z).length() < radius:
			best = maxf(best, v.y)
	return best
