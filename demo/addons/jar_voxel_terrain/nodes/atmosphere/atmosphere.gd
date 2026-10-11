@tool
class_name JarAtmosphere
extends Node3D
## Physically based atmosphere (Rayleigh and Mie single scattering) around a
## planet volume: blue sky and red sunsets from the ground, a glowing rim from
## space, and haze over distant terrain. Several atmospheres can be used at
## once (one per planet), and transparent objects such as water get hazed too.
##
## It reads the volume's up field (planet center and sea level radius); the
## atmosphere's thickness is a fraction of that radius, so doubling the radius
## keeps the look. Tweak colors and density through [member material]
## (atmosphere.gdshader).

const ATMOSPHERE_SHADER := preload("res://addons/jar_voxel_terrain/nodes/atmosphere/atmosphere.gdshader")

## The planet volume (radial up mode). Empty: the parent if it is a volume,
## else the first volume among the siblings.
@export var volume: JarVoxelVolume
## The sun. Empty: the first DirectionalLight3D in the scene.
@export var sun: DirectionalLight3D
## Atmosphere height as a fraction of the planet radius.
@export_range(0.01, 1.0) var thickness := 0.12
## Atmosphere material; empty uses one with the default shader (not saved with
## the scene, so changes to the shader's defaults apply). Give your own a high
## render priority so it draws after water and other transparent objects.
@export var material: ShaderMaterial:
	set(value):
		material = value
		if _shell:
			_shell.material_override = get_active_material()

var _shell: MeshInstance3D
var _default_material: ShaderMaterial

## The material in use: [member material], or the default one.
func get_active_material() -> ShaderMaterial:
	if material:
		return material
	if _default_material == null:
		_default_material = ShaderMaterial.new()
		_default_material.shader = ATMOSPHERE_SHADER
		# After opaque and transparent geometry (water), so it covers them.
		_default_material.render_priority = 100
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
	var root := get_tree().edited_scene_root if Engine.is_editor_hint() else get_tree().current_scene
	if root:
		var lights := root.find_children("*", "DirectionalLight3D", true, false)
		if lights.size() > 0:
			return lights[0]
	return null

func _process(_delta: float) -> void:
	var v := _find_volume()
	if v == null:
		return
	var mat := get_active_material()
	var info: Dictionary = v.get_up_field_info()
	var to_world := v.global_transform
	var center: Vector3 = to_world * info.center
	var radius: float = info.sea_level * to_world.basis.get_scale().x
	var outer := radius * (1.0 + thickness)
	# A little larger than the atmosphere, so its back faces always cover it.
	if not (_shell.mesh is SphereMesh) or not is_equal_approx(_shell.mesh.radius, outer * 1.02):
		var sphere := SphereMesh.new()
		sphere.radius = outer * 1.02
		sphere.height = outer * 2.04
		sphere.radial_segments = 64
		sphere.rings = 32
		_shell.mesh = sphere
	_shell.global_transform = Transform3D(Basis(), center)
	mat.set_shader_parameter("planet_center", center)
	mat.set_shader_parameter("planet_radius", radius)
	mat.set_shader_parameter("atmosphere_radius", outer)
	var light := _find_sun()
	if light:
		mat.set_shader_parameter("sun_direction", light.global_basis.z.normalized())
