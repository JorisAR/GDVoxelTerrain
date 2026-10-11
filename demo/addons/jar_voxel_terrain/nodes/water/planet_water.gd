@tool
class_name JarPlanetWater
extends Node3D
## Water at a JarVoxelVolume's sea level: a sphere for planets (radial up
## mode), a plane for flat worlds. It reads the volume's up field, so it follows
## the terrain's radius and sea level (and the volume when it moves).
##
## Drawn as a coarse mesh for distant water plus a dense patch that follows the
## camera; the shader projects both onto the exact sea level, so the coast is
## precise up close. Tweak the look through [member material] (water.gdshader).

const WATER_SHADER := preload("res://addons/jar_voxel_terrain/nodes/water/water.gdshader")
const UNDERWATER_SHADER := preload("res://addons/jar_voxel_terrain/nodes/water/underwater.gdshader")

## The volume whose sea level this water fills. Empty: the parent if it is a
## volume, else the first volume among the siblings.
@export var volume: JarVoxelVolume
## Meters above (or below) the volume's sea level.
@export var level_offset := 0.0
## Water material; empty uses one with the default water shader (not saved
## with the scene, so changes to the shader's defaults apply).
@export var material: ShaderMaterial:
	set(value):
		material = value
		_apply_material()
## Size of the dense patch around the camera, in meters.
@export var patch_size := 1200.0
## Quads per side of the dense patch.
@export_range(16, 512) var patch_resolution := 200
## Fog when the camera is under water; empty uses the default
## (underwater.gdshader, not saved with the scene).
@export var underwater_material: ShaderMaterial

var _far: MeshInstance3D
var _near: MeshInstance3D
var _fog: MeshInstance3D
var _default_material: ShaderMaterial
var _default_underwater: ShaderMaterial

## The material in use: [member material], or the default one.
func get_active_material() -> ShaderMaterial:
	if material:
		return material
	if _default_material == null:
		_default_material = ShaderMaterial.new()
		_default_material.shader = WATER_SHADER
	return _default_material

func get_active_underwater_material() -> ShaderMaterial:
	if underwater_material:
		return underwater_material
	if _default_underwater == null:
		_default_underwater = ShaderMaterial.new()
		_default_underwater.shader = UNDERWATER_SHADER
		# After the water surface, before clouds and the atmosphere.
		_default_underwater.render_priority = 10
	return _default_underwater

func _ready() -> void:
	# A small sphere around the camera: the fog shader covers the screen from it.
	_fog = MeshInstance3D.new()
	_fog.top_level = true
	_fog.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var bubble := SphereMesh.new()
	bubble.radius = 2.0
	bubble.height = 4.0
	bubble.radial_segments = 16
	bubble.rings = 8
	_fog.mesh = bubble
	_fog.extra_cull_margin = 16384.0
	add_child(_fog, false, Node.INTERNAL_MODE_FRONT)
	_far = MeshInstance3D.new()
	_near = MeshInstance3D.new()
	for m in [_far, _near]:
		m.top_level = true
		m.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
		add_child(m, false, Node.INTERNAL_MODE_FRONT)
	var plane := PlaneMesh.new()
	plane.size = Vector2.ONE * patch_size
	plane.subdivide_width = patch_resolution
	plane.subdivide_depth = patch_resolution
	_near.mesh = plane
	_apply_material()

func _apply_material() -> void:
	if _far:
		_far.material_override = get_active_material()
		_near.material_override = get_active_material()

func _find_volume() -> JarVoxelVolume:
	if volume:
		return volume
	if get_parent() is JarVoxelVolume:
		return get_parent()
	for sibling in get_parent().get_children():
		if sibling is JarVoxelVolume:
			return sibling
	return null

func _process(_delta: float) -> void:
	var v := _find_volume()
	var camera := get_viewport().get_camera_3d() if get_viewport() else null
	if v == null:
		return
	var mat := get_active_material()
	var info: Dictionary = v.get_up_field_info()
	var radial: bool = info.mode == JarVoxelVolume.UP_MODE_RADIAL
	var to_world := v.global_transform
	var center: Vector3 = to_world * info.center
	var axis: Vector3 = (to_world.basis * info.axis).normalized()
	var scale := to_world.basis.get_scale().x
	var level: float = (info.sea_level + level_offset) * scale
	mat.set_shader_parameter("water_center", center)
	mat.set_shader_parameter("water_axis", axis)
	mat.set_shader_parameter("water_level", level)
	mat.set_shader_parameter("water_radial", radial)
	var fog := get_active_underwater_material()
	_fog.material_override = fog
	fog.set_shader_parameter("water_center", center)
	fog.set_shader_parameter("water_axis", axis)
	fog.set_shader_parameter("water_level", level)
	fog.set_shader_parameter("water_radial", radial)

	var eye := camera.global_position if camera else center + axis * (level + 100.0)
	_fog.global_position = eye
	if radial:
		if not (_far.mesh is SphereMesh) or not is_equal_approx(_far.mesh.radius, level):
			var sphere := SphereMesh.new()
			sphere.radius = level
			sphere.height = level * 2.0
			sphere.radial_segments = 192
			sphere.rings = 96
			_far.mesh = sphere
		_far.global_transform = Transform3D(Basis(), center)
		# The patch lies on the tangent plane under the camera; the shader
		# bends it onto the sphere.
		var up := (eye - center).normalized()
		_near.global_transform = Transform3D(_basis_with_up(up), center + up * level)
		# The dense patch sits on top of the coarse sphere, whose flat facets
		# lie slightly inside the sphere: no z-fighting.
		_near.visible = (eye - center).length() - level < patch_size
		_set_hole(_near.global_position if _near.visible else center, patch_size * 0.5 if _near.visible else 0.0)
	else:
		if not (_far.mesh is PlaneMesh) or _far.mesh == _near.mesh:
			var big := PlaneMesh.new()
			big.size = Vector2.ONE * 100000.0
			big.subdivide_width = 64
			big.subdivide_depth = 64
			_far.mesh = big
		var on_plane := eye - axis * (eye - center).dot(axis)
		var basis := _basis_with_up(axis)
		_far.global_transform = Transform3D(basis, on_plane - axis * 0.05)
		# Snapped to the patch grid so the vertices do not swim.
		var step := patch_size / patch_resolution
		on_plane = on_plane.snapped(Vector3.ONE * step)
		_near.global_transform = Transform3D(basis, on_plane)
		_near.visible = true
		_set_hole(on_plane, patch_size * 0.5)

func _set_hole(at: Vector3, radius: float) -> void:
	_far.set_instance_shader_parameter("hole_center", at)
	_far.set_instance_shader_parameter("hole_radius", radius)

func _basis_with_up(up: Vector3) -> Basis:
	var x := up.cross(Vector3.RIGHT if absf(up.x) < 0.9 else Vector3.FORWARD).normalized()
	return Basis(x, up, x.cross(up).normalized())
