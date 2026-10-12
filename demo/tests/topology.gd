class_name Topology
## Welds all chunk triangles of a volume near a point and counts open edges
## (holes / cracks) and edges shared by more than two triangles (non-manifold).

## Identical triangles emitted by two chunks (the shared border band) are
## counted once and reported as `duplicates`.
## Returns {open, nonmanifold, triangles, duplicates}. Only edges within `radius` of
## `center` are considered, so the unmeshed far boundary does not count.
static func analyze(volume: JarVoxelVolume, center: Vector3, radius: float) -> Dictionary:
	var edges := {}
	var seen := {}
	var triangles := 0
	var duplicates := 0
	var weld := 1000.0 # 1 mm
	for c in volume.get_children():
		if not (c is JarVoxelChunk and c.visible):
			continue
		var mesh: Mesh = c.get_mesh_instance().mesh
		if mesh.get_surface_count() == 0:
			continue
		var arrays := mesh.surface_get_arrays(0)
		var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
		var keys := PackedStringArray()
		keys.resize(verts.size())
		for i in verts.size():
			var w: Vector3 = c.position + verts[i]
			keys[i] = "%d,%d,%d" % [roundi(w.x * weld), roundi(w.y * weld), roundi(w.z * weld)]
		for t in range(0, indices.size(), 3):
			var a := indices[t]
			var b := indices[t + 1]
			var d := indices[t + 2]
			if keys[a] == keys[b] or keys[b] == keys[d] or keys[a] == keys[d]:
				continue # degenerate
			var sorted_keys := [keys[a], keys[b], keys[d]]
			sorted_keys.sort()
			var tri_key := "|".join(sorted_keys)
			if seen.has(tri_key):
				duplicates += 1
				continue
			seen[tri_key] = true
			triangles += 1
			for e in [[a, b], [b, d], [d, a]]:
				var mid: Vector3 = c.position + (verts[e[0]] + verts[e[1]]) * 0.5
				if mid.distance_to(center) > radius:
					continue
				var k: String = keys[e[0]] + "|" + keys[e[1]] if keys[e[0]] < keys[e[1]] else keys[e[1]] + "|" + keys[e[0]]
				edges[k] = edges.get(k, 0) + 1
	var open := 0
	var nonmanifold := 0
	for k in edges:
		if edges[k] == 1:
			open += 1
		elif edges[k] > 2:
			nonmanifold += 1
	return {open = open, nonmanifold = nonmanifold, triangles = triangles, duplicates = duplicates}

## Vertices that neighbouring chunks of the same LOD share (same position)
## must have the same normal, or the shading shows the chunk borders. For
## split (crease) vertices each copy is compared with the closest copy in the
## other chunk. Returns {shared, mismatched} (over `degrees`).
static func seam_normals(volume: JarVoxelVolume, center: Vector3, radius: float, degrees := 5.0) -> Dictionary:
	var at := {}
	for c in volume.get_children():
		if not (c is JarVoxelChunk and c.visible):
			continue
		var mesh: Mesh = c.get_mesh_instance().mesh
		if mesh == null or mesh.get_surface_count() == 0:
			continue
		var arrays := mesh.surface_get_arrays(0)
		var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
		for i in verts.size():
			var w: Vector3 = c.position + verts[i]
			if w.distance_to(center) > radius:
				continue
			var key := "%d,%d,%d,%d" % [roundi(w.x * 1000.0), roundi(w.y * 1000.0), roundi(w.z * 1000.0), c.get_lod()]
			if not at.has(key):
				at[key] = {}
			if not at[key].has(c):
				at[key][c] = []
			at[key][c].append(normals[i].normalized())
	var shared := 0
	var mismatched := 0
	for key in at:
		var chunks: Dictionary = at[key]
		if chunks.size() < 2:
			continue
		var list := chunks.keys()
		for a in range(1, list.size()):
			for n: Vector3 in chunks[list[a]]:
				var best := 180.0
				for m: Vector3 in chunks[list[0]]:
					best = minf(best, rad_to_deg(n.angle_to(m)))
				shared += 1
				mismatched += 1 if best > degrees else 0
	return {shared = shared, mismatched = mismatched}
