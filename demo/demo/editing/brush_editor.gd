class_name BrushEditor
extends SdfEditor
## SdfEditor with a few built-in brushes for testing. Each brush pairs a unit
## sized SDF with a mesh of exactly the same shape, so the preview shows what
## the edit will produce.
##
## Q: next brush   scroll: brush size   E: smooth blending   1-4: material
## R: toggle preview

@export var size := 3.0
@export var min_size := 0.25
@export var max_size := 64.0

var _brushes: Array[Dictionary] = []
var _index := 0

func _ready() -> void:
	_brushes = [_sphere(), _box(), _cone(), _mesh_brush("prism", _prism_mesh()), _mesh_brush("torus", _torus_mesh())]
	super()
	_select(0)

func _unhandled_input(event: InputEvent) -> void:
	# Ctrl + scroll is left to the player (fly speed).
	if event is InputEventMouseButton and event.pressed and not event.ctrl_pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			size = minf(size * 1.25, max_size)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			size = maxf(size / 1.25, min_size)
		scale_factor = size

func _unhandled_key_input(event: InputEvent) -> void:
	super(event)
	if not (event is InputEventKey and event.pressed and not event.echo):
		return
	var idx: int = event.keycode - KEY_1
	if idx >= 0 and idx <= 3:
		material = idx
	elif event.keycode == KEY_Q:
		_select((_index + 1) % _brushes.size())
	elif event.keycode == KEY_E:
		smoothness = 0.0 if smoothness > 0.0 else 0.3

func describe() -> String:
	return "%s %.2f m  mat %d%s" % [_brushes[_index].name, size, material, "  smooth" if smoothness > 0.0 else ""]

func _select(index: int) -> void:
	_index = index
	sdf = _brushes[index].sdf
	preview_mesh = _brushes[index].mesh
	scale_factor = size

# All brushes fit a unit cube centered on the origin.

func _sphere() -> Dictionary:
	var s := JarSphereSdf.new()
	s.radius = 0.5
	var m := SphereMesh.new()
	m.radius = 0.5
	m.height = 1.0
	return {name = "sphere", sdf = s, mesh = m}

func _box() -> Dictionary:
	var s := JarBoxSdf.new()
	s.extent = Vector3.ONE * 0.5
	var m := BoxMesh.new()
	m.size = Vector3.ONE
	return {name = "box", sdf = s, mesh = m}

func _cone() -> Dictionary:
	var s := JarConeSdf.new()
	s.height = 1.0
	s.bottom_radius = 0.5
	s.top_radius = 0.0
	var m := CylinderMesh.new()
	m.height = 1.0
	m.bottom_radius = 0.5
	m.top_radius = 0.0
	return {name = "cone", sdf = s, mesh = m}

func _prism_mesh() -> Mesh:
	var m := PrismMesh.new()
	m.size = Vector3.ONE
	return m

func _torus_mesh() -> Mesh:
	var m := TorusMesh.new()
	m.inner_radius = 0.25
	m.outer_radius = 0.5
	return m

## Any closed mesh becomes a brush through JarMeshSdf.
func _mesh_brush(brush_name: String, mesh: Mesh) -> Dictionary:
	var s := JarMeshSdf.new()
	s.mesh = mesh
	s.resolution = 32
	return {name = brush_name, sdf = s, mesh = mesh}
