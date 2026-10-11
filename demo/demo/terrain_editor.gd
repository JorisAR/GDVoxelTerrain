extends Node3D
## Demo terrain brush. Left click digs, right click adds.
## Q cycles the brush shape, E toggles smooth blending, 1-4 pick the material.

@export var terrain: JarVoxelVolume
@export var sdf: JarSphereSdf ## unused, kept so existing scenes still load
@export var edit_radius: float = 50.0

enum Brush { SPHERE, BOX, CONE, PRISM_MESH }
const BRUSH_NAMES := ["sphere", "box", "cone", "prism mesh"]

# Material painted by edits: 0 dirt, 1 stone, 2 ore, 3 crystal (keys 1-4).
var material := 0
var brush := Brush.SPHERE
var smooth := false

var _brushes: Array[JarSignedDistanceField] = []
var edit_timer = 0.0

func _ready() -> void:
	# Unit-sized shapes; the edit transform scales them to edit_radius.
	var sphere := JarSphereSdf.new()
	var box := JarBoxSdf.new()
	box.extent = Vector3(0.8, 0.8, 0.8)
	box.rounding = 0.1
	var cone := JarConeSdf.new()
	cone.height = 2.0
	var prism := JarMeshSdf.new()
	prism.mesh = PrismMesh.new()
	prism.mesh.size = Vector3(1.6, 1.6, 1.6)
	prism.resolution = 32
	_brushes = [sphere, box, cone, prism]

func _physics_process(delta: float) -> void:
	if Input.is_action_pressed("left_click"):
		_edit(false);
	if Input.is_action_pressed("right_click"):
		_edit(true)

	edit_timer -= delta

func _unhandled_key_input(event: InputEvent) -> void:
	if not (event is InputEventKey and event.pressed and not event.echo):
		return
	var idx: int = event.keycode - KEY_1
	if idx >= 0 and idx <= 3:
		material = idx
		print("Edit material: ", ["dirt", "stone", "ore", "crystal"][idx])
	elif event.keycode == KEY_Q:
		brush = (brush + 1) % Brush.size() as Brush
		print("Brush: ", BRUSH_NAMES[brush])
	elif event.keycode == KEY_E:
		smooth = not smooth
		print("Smooth blending: ", smooth)

func _edit(union : bool):
	if(edit_timer > 0):
		return;
	edit_timer = 0.05
	var origin = global_position;
	var direction = -global_transform.basis.z;
	var space_state = get_world_3d().direct_space_state
	var query = PhysicsRayQueryParameters3D.create(origin, origin + direction * 1000)
	var result = space_state.intersect_ray(query)
	if not result:
		return
	# Edit whichever volume was hit: collider -> chunk -> volume.
	var volume := terrain
	var chunk := result.collider.get_parent() as JarVoxelChunk
	if chunk and chunk.get_volume():
		volume = chunk.get_volume()

	var operation := JarVoxelVolume.SDF_OPERATION_UNION if union else JarVoxelVolume.SDF_OPERATION_SUBTRACTION
	if smooth:
		operation = JarVoxelVolume.SDF_OPERATION_SMOOTH_UNION if union else JarVoxelVolume.SDF_OPERATION_SMOOTH_SUBTRACTION
	# Stand the brush on the surface, facing the camera.
	var up: Vector3 = result.normal
	var forward := direction.slide(up).normalized()
	if forward.is_zero_approx():
		forward = up.cross(Vector3.RIGHT).normalized()
	var basis := Basis(forward.cross(up).normalized(), up, -forward).scaled(Vector3.ONE * edit_radius)
	volume.edit(_brushes[brush], operation, Transform3D(basis, result.position), material, edit_radius * 0.3 if smooth else 0.0)
