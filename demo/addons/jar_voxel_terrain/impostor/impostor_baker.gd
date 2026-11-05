@tool
class_name JarImpostorBaker
extends RefCounted
## Bakes octahedral impostors with nothing but Godot: a scene's most detailed
## meshes are rendered from directions on a hemi-octahedron (orthographic, the
## scene's own materials, whatever their shaders) into two atlases: albedo
## unshaded, and normals as the renderer sees them (normal maps included).
## Normals stay relative to each frame's camera; impostor.gdshader turns them
## back, per pixel; their alpha holds ambient occlusion. Needs a window and the Forward+ renderer (the normal buffer).

const SHADER := preload("res://addons/jar_voxel_terrain/shaders/impostor.gdshader")

## Called with the fraction done while baking.
signal progress(amount: float)

var frames := 10 ## per atlas side
var frame_size := 256 ## pixels
var supersampling := 2 ## rendered this many times larger, then scaled down

## Same directions and axes as impostor.gdshader.
static func grid_to_direction(g: Vector2) -> Vector3:
	var p := Vector3(g.x - g.y, 0.0, -1.0 + g.x + g.y)
	p.y = 1.0 - absf(p.x) - absf(p.z)
	return p.normalized()

static func frame_basis(z: Vector3) -> Basis:
	var up := Vector3(0, 0, -1) if absf(z.y) > 0.999 else Vector3.UP
	var x := up.cross(z).normalized()
	var down := x.cross(z).normalized()
	return Basis(x, -down, z)

## The most detailed look of `root`: nodes drawn from distance 0, not impostors.
static func _source_meshes(root: Node) -> Array[MeshInstance3D]:
	var result: Array[MeshInstance3D] = []
	for n: MeshInstance3D in root.find_children("*", "MeshInstance3D", true, false):
		var impostor: bool = n.mesh is QuadMesh and n.name.contains("impostor")
		if n.mesh != null and n.visibility_range_begin <= 0.0 and not impostor:
			result.append(n)
	if root is MeshInstance3D and root.mesh != null:
		result.append(root)
	return result

static func _bounds(meshes: Array[MeshInstance3D], root: Node3D) -> AABB:
	var bounds := AABB()
	var first := true
	for n in meshes:
		var box: AABB = root.global_transform.affine_inverse() * n.global_transform * n.get_aabb()
		bounds = box if first else bounds.merge(box)
		first = false
	return bounds

func _capture(viewport: SubViewport) -> Image:
	viewport.render_target_update_mode = SubViewport.UPDATE_ONCE
	await RenderingServer.frame_post_draw
	var image := viewport.get_texture().get_image()
	image.convert(Image.FORMAT_RGBA8)
	return image

## What a feature looks like: its scene, or its mesh.
static func _feature_source(feature: JarTerrainFeature) -> Node3D:
	if feature.scene != null:
		return feature.scene.instantiate()
	if feature.mesh == null:
		return null
	var node := MeshInstance3D.new()
	node.mesh = feature.mesh
	node.material_override = feature.material
	return node

## Bakes `feature` and saves it (compressed, one file) next to its resource,
## or `folder` if the feature is not a file of its own. Returns an error.
func bake_feature(feature: JarTerrainFeature, parent: Node, folder := "") -> Error:
	var source := _feature_source(feature)
	if source == null:
		return ERR_INVALID_DATA
	var base := feature.resource_path.get_basename()
	if feature.resource_path == "" or feature.resource_path.contains("::"):
		if folder == "":
			source.free()
			return ERR_FILE_BAD_PATH
		base = folder.path_join(feature.resource_name if feature.resource_name != "" else "feature")
	var quad := await bake(source, parent, true)
	var path := base + "_impostor.res"
	var error := ResourceSaver.save(quad, path, ResourceSaver.FLAG_COMPRESS)
	if error != OK:
		return error
	feature.impostor = load(path)
	if feature.resource_path != "" and not feature.resource_path.contains("::"):
		error = ResourceSaver.save(feature)
	return error

## Bakes `source` (freed afterwards) under `parent` (any node in the tree).
## Returns the impostor as a mesh with its material.
func bake(source: Node3D, parent: Node, compress := false) -> QuadMesh:
	# The editor only draws on changes unless told otherwise.
	var low_processor := OS.low_processor_usage_mode
	OS.low_processor_usage_mode = false
	var viewport := SubViewport.new()
	viewport.size = Vector2i.ONE * frame_size * supersampling
	viewport.own_world_3d = true
	viewport.world_3d = World3D.new()
	var environment := Environment.new()
	environment.ssao_enabled = true
	viewport.world_3d.environment = environment
	viewport.transparent_bg = true
	viewport.render_target_update_mode = SubViewport.UPDATE_DISABLED
	parent.add_child(viewport)
	var root := source
	viewport.add_child(root)
	var meshes := _source_meshes(root)
	for n: Node3D in root.find_children("*", "GeometryInstance3D", true, false) + [root]:
		if not n is GeometryInstance3D:
			continue
		n.visible = n in meshes or n == root
		n.visibility_range_begin = 0.0
		n.visibility_range_end = 0.0
	var bounds := _bounds(meshes, root)
	var center := bounds.get_center()
	var radius := bounds.size.length() * 0.5
	environment.ssao_radius = radius * 0.15
	environment.ssao_intensity = 1.2
	environment.ssao_power = 1.0
	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = radius * 2.0
	camera.near = 0.01
	camera.far = radius * 4.0
	viewport.add_child(camera)
	camera.current = true

	var atlas_size := frames * frame_size
	var albedo := Image.create(atlas_size, atlas_size, false, Image.FORMAT_RGBA8)
	var normal := Image.create(atlas_size, atlas_size, false, Image.FORMAT_RGBA8)
	var whole := Rect2i(Vector2i.ZERO, viewport.size)
	var empty := Image.create(whole.size.x, whole.size.y, false, Image.FORMAT_RGBA8)
	for y in frames:
		for x in frames:
			var z := grid_to_direction(Vector2(x, y) / float(frames - 1))
			camera.global_transform = Transform3D(frame_basis(z), center + z * radius * 2.0)
			viewport.debug_draw = Viewport.DEBUG_DRAW_UNSHADED
			var color: Image = await _capture(viewport)
			viewport.debug_draw = Viewport.DEBUG_DRAW_NORMAL_BUFFER
			var normals: Image = await _capture(viewport)
			viewport.debug_draw = Viewport.DEBUG_DRAW_SSAO
			var occlusion: Image = await _capture(viewport)
			# Only where the object is, then spread outwards: filtered
			# edges and mipmaps must not pull in the empty background.
			normals.convert(Image.FORMAT_RGB8)
			normals.convert(Image.FORMAT_RGBA8)
			var masked: Image = empty.duplicate()
			masked.blit_rect_mask(normals, color, whole, Vector2i.ZERO)
			color.fix_alpha_edges()
			masked.fix_alpha_edges()
			color.resize(frame_size, frame_size, Image.INTERPOLATE_BILINEAR)
			masked.resize(frame_size, frame_size, Image.INTERPOLATE_BILINEAR)
			occlusion.resize(frame_size, frame_size, Image.INTERPOLATE_BILINEAR)
			occlusion.convert(Image.FORMAT_L8)
			var from := occlusion.get_data()
			var to := masked.get_data()
			for i in from.size():
				to[i * 4 + 3] = from[i]
			masked = Image.create_from_data(frame_size, frame_size, false, Image.FORMAT_RGBA8, to)
			var at := Vector2i(x, y) * frame_size
			albedo.blit_rect(color, Rect2i(0, 0, frame_size, frame_size), at)
			normal.blit_rect(masked, Rect2i(0, 0, frame_size, frame_size), at)
			progress.emit(float(y * frames + x + 1) / float(frames * frames))
	viewport.queue_free()
	OS.low_processor_usage_mode = low_processor
	albedo.generate_mipmaps()
	normal.generate_mipmaps()
	if compress:
		albedo.compress(Image.COMPRESS_BPTC, Image.COMPRESS_SOURCE_SRGB)
		normal.compress(Image.COMPRESS_BPTC, Image.COMPRESS_SOURCE_GENERIC)

	var material := ShaderMaterial.new()
	material.shader = SHADER
	material.set_shader_parameter("imposterFrames", Vector2(frames, frames))
	material.set_shader_parameter("positionOffset", center)
	material.set_shader_parameter("scale", radius)
	material.set_shader_parameter("imposterTextureAlbedo", ImageTexture.create_from_image(albedo))
	material.set_shader_parameter("imposterTextureNormal", ImageTexture.create_from_image(normal))
	material.set_shader_parameter("use_baked_normals", true)
	var quad := QuadMesh.new()
	quad.size = Vector2.ONE * radius * 2.0
	quad.center_offset = center
	quad.custom_aabb = AABB(center - Vector3.ONE * radius, Vector3.ONE * radius * 2.0)
	quad.material = material
	return quad
