class_name Topology

## Whether chunk `c` can reach within `radius` of `center` (volume space):
## skipping the rest keeps checks in large worlds cheap (all chunks of a
## planet are millions of triangles).
static func _near(volume: JarVoxelVolume, c: JarVoxelChunk, center: Vector3, radius: float) -> bool:
	var half_diagonal := 0.87 * 16.0 * volume.voxel_size * pow(2.0, c.get_lod())
	return c.position.distance_to(center) <= radius + half_diagonal
## Welds all chunk triangles of a volume near a point and counts open edges
## (holes / cracks) and edges shared by more than two triangles (non-manifold).

## Identical triangles emitted by two chunks (the shared border band) are
## counted once and reported as `duplicates`.
## Returns {open, nonmanifold, triangles, duplicates, open_at} (open_at: up to
## 12 [midpoint, chunk LOD] of open edges). Only edges within `radius` of
## `center` are considered, so the unmeshed far boundary does not count.
static func analyze(volume: JarVoxelVolume, center: Vector3, radius: float) -> Dictionary:
	var edges := {}
	var where := {} # edge -> [midpoint, chunk LOD]
	var seen := {}
	var triangles := 0
	var duplicates := 0
	var weld := 1000.0 # 1 mm
	for c in volume.get_children():
		if not (c is JarVoxelChunk and c.visible and _near(volume, c, center, radius)):
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
				where[k] = [mid, c.get_lod()]
	var open := 0
	var nonmanifold := 0
	var open_at := []
	for k in edges:
		if edges[k] == 1:
			open += 1
			if open_at.size() < 12:
				open_at.append(where[k])
		elif edges[k] > 2:
			nonmanifold += 1
	return {open = open, nonmanifold = nonmanifold, triangles = triangles, duplicates = duplicates, open_at = open_at}

## Vertices that neighbouring chunks of the same LOD share (same position)
## must have the same normal, or the shading shows the chunk borders. For
## split (crease) vertices each copy is compared with the closest copy in the
## other chunk. Returns {shared, mismatched} (over `degrees`).
static func seam_normals(volume: JarVoxelVolume, center: Vector3, radius: float, degrees := 5.0) -> Dictionary:
	var at := {}
	for c in volume.get_children():
		if not (c is JarVoxelChunk and c.visible and _near(volume, c, center, radius)):
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

## Triangle quality near `center` (LOD 0 chunks unless `all_lods`): `flipped`
## faces whose normal points into the ground (against the SDF normal at their
## centroid), `off_surface` vertices further than half a voxel (of their LOD)
## from the SDF surface (`reference`, else the volume's SDF: pass the edited
## shape for edited terrain),
## `slivers` with a smallest angle under 2 degrees, `misaligned` faces turned
## over 40 degrees from the surface at all three corners (teeth). `worst` lists the world
## positions of flipped faces (for screenshots).
static func quality(volume: JarVoxelVolume, center: Vector3, radius: float, all_lods := false, reference: JarSignedDistanceField = null) -> Dictionary:
	var sdf := reference if reference else volume.sdf
	var voxel := volume.voxel_size
	var flipped := 0
	var off_surface := 0
	var slivers := 0
	var misaligned := 0
	var worst_misaligned := []
	var corners := []
	var triangles := 0
	var worst := []
	for c in volume.get_children():
		if not (c is JarVoxelChunk and c.visible and (all_lods or c.get_lod() == 0) and _near(volume, c, center, radius)):
			continue
		var mesh: Mesh = c.get_mesh_instance().mesh
		if mesh == null or mesh.get_surface_count() == 0:
			continue
		var arrays := mesh.surface_get_arrays(0)
		var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
		var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
		var tolerance := 0.5 * voxel * pow(2.0, c.get_lod())
		var origin: Vector3 = volume.global_transform.affine_inverse() * c.global_position
		for t in range(0, indices.size(), 3):
			var a: Vector3 = origin + verts[indices[t]]
			var b: Vector3 = origin + verts[indices[t + 1]]
			var d: Vector3 = origin + verts[indices[t + 2]]
			var mid := (a + b + d) / 3.0
			if mid.distance_to(center) > radius:
				continue
			triangles += 1
			var n := (d - a).cross(b - a)
			if n.length() < 1e-9:
				slivers += 1
				continue
			# Godot's front faces wind clockwise: the outward normal is (d - a) x (b - a).
			if n.normalized().dot(sdf.sample_normal(mid)) < 0.0:
				flipped += 1
				if worst.size() < 20:
					worst.append(mid)
			# A tooth: the face turns well away from the surface at all its corners
			# (sharp edges are fine: faces there match one side or the other).
			var face := n.normalized()
			var best := -1.0
			for p in [a, b, d]:
				best = maxf(best, face.dot(sdf.sample_normal(p)))
			if best < cos(deg_to_rad(40.0)):
				misaligned += 1
				if worst_misaligned.size() < 12:
					worst_misaligned.append(mid)
					corners.append([a, b, d])
			var smallest := minf(minf((b - a).angle_to(d - a), (a - b).angle_to(d - b)), (a - d).angle_to(b - d))
			if rad_to_deg(smallest) < 2.0:
				slivers += 1
			for p in [a, b, d]:
				if absf(sdf.sample(p)) > tolerance:
					off_surface += 1
	return {triangles = triangles, flipped = flipped, off_surface = off_surface, slivers = slivers, misaligned = misaligned, worst = worst, worst_misaligned = worst_misaligned, misaligned_corners = corners}
