extends Node3D

@export var terrain: JarVoxelTerrain
@export var sdf: JarSphereSdf

var edit_timer = 0.0
func _physics_process(delta: float) -> void:
	if Input.is_action_pressed("left_click"):
		_edit(false);
	if Input.is_action_pressed("right_click"):
		_edit(true)
		
	edit_timer -= delta
					   
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
		#terrain.modify(sdf, )
		#terrain.modify(sdf, 0, result.position, 10)
	#OPERATION
		terrain.sphere_edit(result.position, 50, union)
		#terrain.spawn_debug_spheres_in_bounds(result.position, 16)
