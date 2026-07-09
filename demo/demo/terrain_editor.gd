extends Node3D

@export var terrain: JarVoxelTerrain
@export var sdf: JarSphereSdf
@export var edit_radius: float = 50.0

# Material painted by edits: 0 dirt, 1 stone, 2 ore, 3 crystal (keys 1-4).
var material := 0

var edit_timer = 0.0
func _physics_process(delta: float) -> void:
	if Input.is_action_pressed("left_click"):
		_edit(false);
	if Input.is_action_pressed("right_click"):
		_edit(true)

	edit_timer -= delta

func _unhandled_key_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		var idx: int = event.keycode - KEY_1
		if idx >= 0 and idx <= 3:
			material = idx
			print("Edit material: ", ["dirt", "stone", "ore", "crystal"][idx])

func _edit(union : bool):
	if(edit_timer > 0):
		return;
	edit_timer = 0.05
	var origin = global_position;
	var direction = -global_transform.basis.z;
	var space_state = get_world_3d().direct_space_state
	var query = PhysicsRayQueryParameters3D.create(origin, origin + direction * 1000)
	var result = space_state.intersect_ray(query)
	if result:
		terrain.sphere_edit(result.position, edit_radius, union, material)
