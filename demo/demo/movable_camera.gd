extends Camera3D

@export var terrain : JarVoxelTerrain
@export var move_speed : float = 8.0
@export var look_sensitivity : float = 0.1
const MIN_MOVE_SPEED = 0.5;
const MAX_MOVE_SPEED = 4096;
var rotating : bool = false
var triangle : bool = false
	
func _ready() -> void:
	set_mouse(true);
	#get_viewport().debug_draw = Viewport.DEBUG_DRAW_WIREFRAME; 
	#get_viewport().debug_draw = Viewport.DEBUG_DRAW_OVERDRAW; 

func _process(delta : float) -> void:
	var _wish_dir_raw = Input.get_vector("move_left", "move_right", "move_forward", "move_backward");
	var up = Vector3.ZERO;
	if Input.is_action_pressed("move_up"):
		up = Vector3.UP;
	if Input.is_action_pressed("move_down"):
		up = Vector3.DOWN;
	var forward = transform.basis.z.normalized();
	var right = transform.basis.x.normalized();
	var move_dir = forward * _wish_dir_raw.y + right * _wish_dir_raw.x + up;
	position += move_dir.normalized() * move_speed * delta / Engine.time_scale;

func _input(event) -> void:
	if event.is_action_pressed("scroll_up"):
		move_speed *= 2;
	if event.is_action_pressed("scroll_down"):
		move_speed *= 0.5;
	move_speed = clamp(move_speed, MIN_MOVE_SPEED, MAX_MOVE_SPEED);
		
	if event.is_action_pressed("ui_cancel"):
		set_mouse(!rotating);
	if event.is_action_pressed("ui_focus_next"):
		toggle_triangle(!triangle)
	if event is InputEventMouseMotion and rotating:
		rotation += Vector3(-event.relative.y * 0.0025, -event.relative.x * 0.0025, 0);
		rotation.x = clamp(rotation.x, -PI/2.2, PI/2.2)
	if event.is_action_pressed("terrain_update_lod"):
		terrain.force_update_lod()

func toggle_triangle(value: bool) -> void:
	triangle = value;
	get_viewport().debug_draw = Viewport.DEBUG_DRAW_WIREFRAME if triangle else Viewport.DEBUG_DRAW_DISABLED

func set_mouse(value: bool) -> void:
	rotating = value;
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED if rotating else Input.MOUSE_MODE_VISIBLE;
	
