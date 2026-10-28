extends Node
## Plays a demo scene; hold B for a second while looking at a problem to save
## a report of it to tests/report/out/: a screenshot, the camera, what the
## crosshair hits, the chunks around it (LOD, triangles, open edges, faces
## folded against the surface) and their triangles as an OBJ (world space, one
## group per chunk), so the case can be studied and reproduced exactly.
## Run from the editor (F6 on bug_report.tscn) or:
##   godot --path demo res://tests/report/bug_report.tscn
## SCENE overrides the scene (default res://demo/planet.tscn).

const HOLD_SECONDS := 1.0
const RADIUS := 30.0 # meters around the hit that are reported
const CHUNK_VOXELS := 16 # JarVoxelVolume::ChunkSizeLog2

var _scene: Node
var _held := 0.0
var _done := false # until B is released
var _label: Label

func _ready() -> void:
	Engine.max_fps = 120
	var path := OS.get_environment("SCENE")
	_scene = load(path if path != "" else "res://demo/planet.tscn").instantiate()
	add_child(_scene)
	var layer := CanvasLayer.new()
	layer.layer = 100
	_label = Label.new()
	_label.position = Vector2(16, 40)
	_label.add_theme_font_size_override("font_size", 22)
	_label.text = "Hold B on a problem to report it"
	layer.add_child(_label)
	add_child(layer)
	if OS.get_environment("AUTO_REPORT") == "1": # smoke test: report once things settle, then quit
		await get_tree().create_timer(15.0).timeout
		await _report()
		get_tree().quit()

func _process(delta: float) -> void:
	if not Input.is_physical_key_pressed(KEY_B):
		_held = 0.0
		_done = false
		return
	if _done:
		return
	_held += delta
	_label.text = "Reporting... %d%%" % int(100.0 * _held / HOLD_SECONDS)
	if _held >= HOLD_SECONDS:
		_done = true
		_report()

func _report() -> void:
	var camera := get_viewport().get_camera_3d()
	var dir := ProjectSettings.globalize_path("res://tests/report/out")
	DirAccess.make_dir_recursive_absolute(dir)
	var stamp := Time.get_datetime_string_from_system().replace(":", "-")
	var base := "%s/report_%s" % [dir, stamp]
	# Screenshot without the label.
	if DisplayServer.get_name() != "headless":
		_label.visible = false
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(base + ".png")
		_label.visible = true

	var from := camera.global_position
	var forward := -camera.global_basis.z
	var query := PhysicsRayQueryParameters3D.create(from, from + forward * 2000.0)
	var player := _scene.get_node_or_null("Player") as CollisionObject3D
	if player:
		query.exclude = [player.get_rid()]
	var hit := camera.get_world_3d().direct_space_state.intersect_ray(query)
	var data := {
		scene = _scene.scene_file_path,
		camera_position = _v(from), camera_forward = _v(forward), camera_up = _v(camera.global_basis.y),
		fov = camera.fov,
		hit = {position = _v(hit.position), normal = _v(hit.normal), collider = str(hit.collider.get_path())} if hit else null,
		volumes = [],
	}
	# Without a physics hit (no collider there) use the SDF ray.
	var center: Vector3 = hit.position if hit else from + forward * 50.0
	var obj := PackedStringArray()
	var vertex_offset := 1
	for volume: JarVoxelVolume in _scene.find_children("*", "JarVoxelVolume", true, false):
		var local := volume.global_transform.affine_inverse() * center
		if not hit:
			var sdf_hit = volume.raycast_sdf(from, from + forward * 2000.0)
			if sdf_hit:
				center = sdf_hit.position
				local = volume.global_transform.affine_inverse() * center
		var info := {
			path = str(volume.get_path()), voxel_size = volume.voxel_size,
			meshing_algorithm = volume.meshing_algorithm, manifold = volume.manifold,
			building = volume.is_building(), meshing = volume.is_meshing(),
			center_local = _v(local), chunks = [],
		}
		for c in volume.get_children():
			if not (c is JarVoxelChunk and c.visible):
				continue
			var mesh: Mesh = c.get_mesh_instance().mesh
			if mesh == null or mesh.get_surface_count() == 0:
				continue
			var size: float = CHUNK_VOXELS * volume.voxel_size * pow(2.0, c.get_lod())
			if c.position.distance_to(local) > RADIUS + size:
				continue
			var arrays := mesh.surface_get_arrays(0)
			var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
			var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
			var collider: CollisionShape3D = c.get_collision_shape()
			info.chunks.append({
				name = str(c.name), lod = c.get_lod(), position = _v(c.position),
				triangles = indices.size() / 3,
				collider = collider != null and not collider.disabled and collider.shape is ConcavePolygonShape3D
					and (collider.shape as ConcavePolygonShape3D).get_faces().size() > 0,
			})
			obj.append("g chunk_%s_lod%d" % [c.name, c.get_lod()])
			for v in verts:
				var w: Vector3 = volume.global_transform * (c.position + v)
				obj.append("v %.4f %.4f %.4f" % [w.x, w.y, w.z])
			for t in range(0, indices.size(), 3):
				obj.append("f %d %d %d" % [indices[t] + vertex_offset, indices[t + 1] + vertex_offset, indices[t + 2] + vertex_offset])
			vertex_offset += verts.size()
		info.topology = Topology.analyze(volume, local, RADIUS)
		var quality := Topology.quality(volume, local, RADIUS, true)
		quality.worst = quality.worst.map(func(p): return _v(volume.global_transform * p))
		info.quality = quality
		data.volumes.append(info)
	FileAccess.open(base + ".json", FileAccess.WRITE).store_string(JSON.stringify(data, "\t"))
	FileAccess.open(base + ".obj", FileAccess.WRITE).store_string("\n".join(obj))
	print("REPORT ", base)
	_label.text = "Saved report_%s (hold B again for another)" % stamp

func _v(v: Vector3) -> Array:
	return [snappedf(v.x, 0.001), snappedf(v.y, 0.001), snappedf(v.z, 0.001)]
