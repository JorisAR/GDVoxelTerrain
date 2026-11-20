extends Node
## Many overlapping turned boxes and spheres, added and dug: open edges and
## folds (neighbouring faces turned back on each other).
var volume: JarVoxelVolume

func _ready() -> void:
	var world := TestWorld.flat(self, Vector3(0, 10, 0), func(v: JarVoxelVolume): v.meshing_algorithm = JarVoxelVolume.MESHING_DUAL_CONTOURING; v.manifold = true)
	volume = world.volume
	await _settle()
	var ground := 0.0
	var hit := volume.raycast_sdf(Vector3(0.3, 500, 0.3), Vector3(0.3, -500, 0.3))
	if hit.has("position"):
		ground = hit.position.y
	var center := Vector3(0.3, ground + 4.0, 0.3)
	world.viewer.position = center + Vector3(0, 6, 0)
	volume.force_update_lod()
	await _settle()
	var rng := RandomNumberGenerator.new()
	rng.seed = int(OS.get_environment("SEED")) if OS.get_environment("SEED") != "" else 1
	for i in 40:
		var shape: JarSignedDistanceField
		if rng.randf() < 0.6:
			var box := JarBoxSdf.new()
			box.extent = Vector3(rng.randf_range(0.5, 3), rng.randf_range(0.5, 3), rng.randf_range(0.5, 3))
			shape = box
		else:
			var s := JarSphereSdf.new()
			s.radius = rng.randf_range(0.8, 3)
			shape = s
		var basis := Basis.from_euler(Vector3(rng.randf() * 6.3, rng.randf() * 6.3, rng.randf() * 6.3))
		var at := center + Vector3(rng.randf_range(-6, 6), rng.randf_range(-3, 5), rng.randf_range(-6, 6))
		var op := JarVoxelVolume.SDF_OPERATION_UNION if rng.randf() < 0.6 else JarVoxelVolume.SDF_OPERATION_SUBTRACTION
		volume.edit(shape, op, Transform3D(basis, at))
	await _settle()
	var r := Topology.analyze(volume, center, 12.0)
	print("RESULT open ", r.open, " folds ", _folds(center, 12.0))
	get_tree().quit()

func _settle() -> void:
	var idle := 0
	while idle < 30:
		await get_tree().physics_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0

func _folds(center: Vector3, radius: float) -> int:
	var edges := {}
	var key := func(v: Vector3) -> Vector3i: return Vector3i((v * 1000.0).round())
	for c in volume.get_children():
		if not (c is JarVoxelChunk and c.visible):
			continue
		var mesh: Mesh = c.get_mesh()
		if mesh == null or mesh.get_surface_count() == 0:
			continue
		var arrays := mesh.surface_get_arrays(0)
		var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var idx: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
		for t in range(0, idx.size(), 3):
			var a: Vector3 = c.position + verts[idx[t]]
			var b: Vector3 = c.position + verts[idx[t + 1]]
			var d: Vector3 = c.position + verts[idx[t + 2]]
			if a.distance_to(center) > radius:
				continue
			var n := (d - a).cross(b - a)
			if n.length() < 1e-9:
				continue
			n = n.normalized()
			for e in [[a, b], [b, d], [d, a]]:
				var ka: Vector3i = key.call(e[0])
				var kb: Vector3i = key.call(e[1])
				var k := [ka, kb] if str(ka) < str(kb) else [kb, ka]
				if not edges.has(k):
					edges[k] = []
				edges[k].append(n)
	var folds := 0
	for k in edges:
		var ns: Array = edges[k]
		if ns.size() == 2 and (ns[0] as Vector3).dot(ns[1]) < -0.85:
			folds += 1
	return folds
