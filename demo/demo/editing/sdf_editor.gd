class_name SdfEditor
extends Node3D
## Edits whichever voxel volume is under this node's forward ray (attach it to a
## camera) with any SDF resource. Left click removes, right click adds.
##
## The SDF is used in its own units, scaled by `scale_factor`, and centered on
## the hit point. Assign `preview_mesh` (same shape and size as the SDF) to see
## where the edit will land; R toggles the preview. In paint mode (P) left click
## paints `material` onto the surface inside the shape instead of editing it.

@export var sdf: JarSignedDistanceField
## Uniform scale applied to the SDF (a unit sphere SDF with scale 2 has radius 2 m).
@export var scale_factor := 1.0
## Material painted by edits (0-7), or -1 to keep existing materials.
@export_range(-1, 7) var material := 0
## Left click paints instead of removing; right click still adds.
@export var paint_mode := false
## Blend radius in meters; 0 gives hard edits, > 0 uses the smooth operations.
@export var smoothness := 0.0
## Orient the shape to the surface normal instead of the volume's up.
@export var align_to_surface := true
@export var max_distance := 200.0
## Seconds between edits while a mouse button is held.
@export var edit_interval := 0.08
@export var preview_mesh: Mesh:
	set(value):
		preview_mesh = value
		if _preview:
			_preview.mesh = value
@export var show_preview := true

const PREVIEW_SHADER := preload("res://demo/editing/brush_preview.gdshader")

var _preview: MeshInstance3D
var _cooldown := 0.0
var _exclude: Array[RID] = []

func _ready() -> void:
	_preview = MeshInstance3D.new()
	_preview.top_level = true
	_preview.mesh = preview_mesh
	_preview.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var mat := ShaderMaterial.new()
	mat.shader = PREVIEW_SHADER
	_preview.material_override = mat
	add_child(_preview)
	# Never hit the character we are attached to.
	var body := _find_body(self)
	if body:
		_exclude.append(body.get_rid())

func _unhandled_key_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_R:
			show_preview = not show_preview
		elif event.keycode == KEY_P:
			paint_mode = not paint_mode

func _physics_process(delta: float) -> void:
	_cooldown -= delta
	var hit := _raycast()
	var target := _edit_transform(hit) if hit else Transform3D()
	_preview.visible = show_preview and hit and preview_mesh != null
	if _preview.visible:
		_preview.global_transform = target
		# Outline width scales with the brush, within sensible limits.
		(_preview.material_override as ShaderMaterial).set_shader_parameter("band_width", clampf(scale_factor * 0.05, 0.05, 1.0))
	if not hit or _cooldown > 0.0 or sdf == null:
		return
	var add := Input.is_action_pressed("right_click")
	var remove := Input.is_action_pressed("left_click")
	if add == remove:
		return
	_cooldown = edit_interval
	var volume := volume_from_hit(hit)
	if volume and paint_mode and remove:
		volume.paint(sdf, target, maxi(material, 0))
	elif volume:
		volume.edit(sdf, _operation(add), target, material, smoothness)

## Describes the current settings, for HUDs.
func describe() -> String:
	return "%s  x%.2f  mat %d%s%s" % [sdf.get_class() if sdf else "no SDF", scale_factor, material,
		"  smooth" if smoothness > 0.0 else "", "  PAINT" if paint_mode else ""]

## The volume owning a physics hit: collider -> chunk -> volume.
static func volume_from_hit(hit: Dictionary) -> JarVoxelVolume:
	var node: Node = hit.collider
	while node:
		if node is JarVoxelChunk:
			return node.get_volume()
		node = node.get_parent()
	return null

func _operation(add: bool) -> JarVoxelVolume.Operation:
	if smoothness > 0.0:
		return JarVoxelVolume.SDF_OPERATION_SMOOTH_UNION if add else JarVoxelVolume.SDF_OPERATION_SMOOTH_SUBTRACTION
	return JarVoxelVolume.SDF_OPERATION_UNION if add else JarVoxelVolume.SDF_OPERATION_SUBTRACTION

func _raycast() -> Dictionary:
	var from := global_position
	var to := from - global_basis.z * max_distance
	var query := PhysicsRayQueryParameters3D.create(from, to)
	query.exclude = _exclude
	return get_world_3d().direct_space_state.intersect_ray(query)

## Centered on the hit, up along the surface (or volume) normal, facing away
## from the camera, scaled by scale_factor.
func _edit_transform(hit: Dictionary) -> Transform3D:
	var up: Vector3 = hit.normal
	if not align_to_surface:
		var volume := volume_from_hit(hit)
		if volume:
			up = volume.get_up_direction_at(hit.position)
	var forward := (-global_basis.z).slide(up).normalized()
	if forward.is_zero_approx():
		forward = up.cross(Vector3.RIGHT).normalized()
	var basis := Basis(forward.cross(up).normalized(), up, -forward).scaled(Vector3.ONE * scale_factor)
	return Transform3D(basis, hit.position)

static func _find_body(node: Node) -> CollisionObject3D:
	while node:
		if node is CollisionObject3D:
			return node
		node = node.get_parent()
	return null
