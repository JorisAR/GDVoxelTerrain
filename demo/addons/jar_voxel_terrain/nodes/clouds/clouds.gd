@tool
class_name JarClouds
extends Node3D
## A basic cloud layer above a JarVoxelVolume: a shell at [member altitude]
## meters above the volume's sea level (a sphere for planets, a plane for flat
## worlds), following its up field like JarPlanetWater and JarAtmosphere.
## Shape, wind and lighting are set on [member material] (clouds.gdshader).

const CLOUD_SHADER := preload("res://addons/jar_voxel_terrain/nodes/clouds/clouds.gdshader")

## The volume. Empty: the parent if it is a volume, else the first volume
## among the siblings.
@export var volume: JarVoxelVolume
## The sun. Empty: the first DirectionalLight3D in the scene.
@export var sun: DirectionalLight3D
## Height of the cloud layer above sea level, in meters.
@export var altitude := 320.0
## Cloud material; empty uses one with the default shader (not saved with the
## scene, so changes to the shader's defaults apply).
@export var material: ShaderMaterial:
	set(value):
		material = value
		if _shell:
			_shell.material_override = get_active_material()

var _shell: MeshInstance3D
var _default_material: ShaderMaterial
var _found_sun: DirectionalLight3D

## The material in use: [member material], or the default one.
func get_active_material() -> ShaderMaterial:
	if material:
		return material
	if _default_material == null:
		_default_material = ShaderMaterial.new()
		_default_material.shader = CLOUD_SHADER
		# After water, before the atmosphere (which hazes the clouds).
		_default_material.render_priority = 50
	return _default_material

func _ready() -> void:
	_shell = MeshInstance3D.new()
	_shell.top_level = true
	_shell.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	_shell.material_override = get_active_material()
	add_child(_shell, false, Node.INTERNAL_MODE_FRONT)

func _find_volume() -> JarVoxelVolume:
	if volume:
		return volume
	if get_parent() is JarVoxelVolume:
		return get_parent()
	for sibling in get_parent().get_children():
		if sibling is JarVoxelVolume:
			return sibling
	return null

func _find_sun() -> DirectionalLight3D:
	if sun:
		return sun
	if is_instance_valid(_found_sun) and _found_sun.is_inside_tree():
		return _found_sun
	_found_sun = null
	var root := get_tree().edited_scene_root if Engine.is_editor_hint() else get_tree().current_scene
	if root:
		var lights := root.find_children("*", "DirectionalLight3D", true, false)
		if lights.size() > 0:
			_found_sun = lights[0]
	return _found_sun

func _process(_delta: float) -> void:
	var v := _find_volume()
	if v == null:
		return
	var mat := get_active_material()
	var info: Dictionary = v.get_up_field_info()
	var radial: bool = info.mode == JarVoxelVolume.UP_MODE_RADIAL
	var to_world := v.global_transform
	var center: Vector3 = to_world * info.center
	var axis: Vector3 = (to_world.basis * info.axis).normalized()
	var level: float = (info.sea_level + altitude) * to_world.basis.get_scale().x
	if radial:
		if not (_shell.mesh is SphereMesh) or not is_equal_approx(_shell.mesh.radius, level):
			var sphere := SphereMesh.new()
			sphere.radius = level
			sphere.height = level * 2.0
			sphere.radial_segments = 192
			sphere.rings = 96
			_shell.mesh = sphere
		_shell.global_transform = Transform3D(Basis(), center)
	else:
		if not (_shell.mesh is PlaneMesh):
			var plane := PlaneMesh.new()
			plane.size = Vector2.ONE * 20000.0
			_shell.mesh = plane
		var camera := get_viewport().get_camera_3d() if get_viewport() else null
		var eye := camera.global_position if camera else center
		var on_plane := eye - axis * (eye - center).dot(axis) + axis * level
		var x := axis.cross(Vector3.RIGHT if absf(axis.x) < 0.9 else Vector3.FORWARD).normalized()
		_shell.global_transform = Transform3D(Basis(x, axis, x.cross(axis).normalized()), on_plane)
	mat.set_shader_parameter("planet_center", center)
	mat.set_shader_parameter("up_axis", axis)
	mat.set_shader_parameter("radial", radial)
	var light := _find_sun()
	if light:
		mat.set_shader_parameter("sun_direction", light.global_basis.z.normalized())
		mat.set_shader_parameter("sun_color", Vector3(light.light_color.r, light.light_color.g, light.light_color.b))
