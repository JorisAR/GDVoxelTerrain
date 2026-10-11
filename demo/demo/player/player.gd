class_name DemoPlayer
extends CharacterBody3D
## First-person capsule character for any volume: it stands along gravity
## (Godot's Area3D point gravity for planets, project gravity otherwise),
## walks, sprints and jumps, and flies with Tab.
##
## WASD: move   Space: jump / fly up   Shift: sprint / fly down
## Tab: toggle flying   Ctrl + scroll: fly speed   Esc: release the mouse

@export var walk_speed := 5.0
@export var sprint_speed := 10.0
@export var jump_speed := 5.5
@export var ground_acceleration := 14.0
@export var air_acceleration := 3.0
@export var fly_speed := 20.0
@export var mouse_sensitivity := 0.0025
## How quickly the body turns to match a new gravity direction (1/s).
@export var align_speed := 8.0
@export var start_flying := false

@onready var head: Node3D = $Head
@onready var hud: Label = $HUD/Label

var flying := false
var _spawned := false
var _moved_to_surface := false

func _ready() -> void:
	flying = start_flying
	floor_snap_length = 0.5
	floor_max_angle = deg_to_rad(50.0)
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		# Yaw turns the body around its own up axis; pitch only tilts the head.
		rotate_object_local(Vector3.UP, -event.relative.x * mouse_sensitivity)
		head.rotation.x = clampf(head.rotation.x - event.relative.y * mouse_sensitivity, -1.5, 1.5)
	elif event.is_action_pressed("ui_cancel"):
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED else Input.MOUSE_MODE_CAPTURED
	elif event.is_action_pressed("ui_focus_next"):
		flying = not flying
	elif event is InputEventMouseButton and event.pressed and event.ctrl_pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			fly_speed = minf(fly_speed * 1.5, 2000.0)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			fly_speed = maxf(fly_speed / 1.5, 1.0)

func _physics_process(delta: float) -> void:
	var gravity := get_gravity()
	if gravity.length_squared() > 1e-6:
		up_direction = -gravity.normalized()
	_align_to(up_direction, delta)

	if not _spawned:
		_spawned = _try_spawn()
		return

	var input := Input.get_vector("move_left", "move_right", "move_forward", "move_backward")
	if flying:
		_fly(input, delta)
	else:
		_walk(input, gravity, delta)
	move_and_slide()
	_update_hud()

## Stands on the ground once the terrain around the spawn point is final.
## Colliders are only built near viewers, and a coarse (distant) collider can
## lie below the real surface, so: move to the surface the volume's SDF
## predicts, wait until every volume has finished building and meshing there,
## then stand on the physics ground.
func _try_spawn() -> bool:
	if start_flying:
		return true
	var volumes := get_tree().root.find_children("*", "JarVoxelVolume", true, false)
	if not _moved_to_surface:
		for volume: JarVoxelVolume in volumes:
			var surface := volume.raycast_sdf(global_position, global_position - up_direction * 10000.0)
			if surface:
				global_position = surface.position + up_direction * 2.0
				volume.force_update_lod() # refine (and build colliders) here now
				break
		_moved_to_surface = true
		return false
	for volume: JarVoxelVolume in volumes:
		if volume.is_building() or volume.is_meshing():
			return false
	var query := PhysicsRayQueryParameters3D.create(global_position + up_direction * 2.0, global_position - up_direction * 2000.0)
	query.exclude = [get_rid()]
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	if not hit:
		return false
	global_position = hit.position + up_direction * 0.1
	velocity = Vector3.ZERO
	return true

## Rotates the body so its up axis follows gravity, keeping the heading.
func _align_to(up: Vector3, delta: float) -> void:
	var current := global_basis.y.normalized()
	if current.dot(up) > 0.99999:
		return
	var turn := Quaternion(current, up)
	var target := Basis(turn) * global_basis
	global_basis = global_basis.orthonormalized().slerp(target.orthonormalized(), clampf(align_speed * delta, 0.0, 1.0))

func _walk(input: Vector2, gravity: Vector3, delta: float) -> void:
	var up := up_direction
	var vertical := up * velocity.dot(up)
	var horizontal := velocity - vertical

	var speed := sprint_speed if Input.is_action_pressed("move_down") else walk_speed
	var wish := (global_basis.x * input.x + global_basis.z * input.y).slide(up)
	if wish.length_squared() > 1e-6:
		wish = wish.normalized() * speed
	var acceleration := ground_acceleration if is_on_floor() else air_acceleration
	horizontal = horizontal.lerp(wish, clampf(acceleration * delta, 0.0, 1.0))

	if is_on_floor():
		vertical = Vector3.ZERO
		if Input.is_action_pressed("move_up"):
			vertical = up * jump_speed
	else:
		vertical += gravity * delta
	velocity = horizontal + vertical

func _fly(input: Vector2, delta: float) -> void:
	var view := head.global_basis
	var dir := view.x * input.x + view.z * input.y
	if Input.is_action_pressed("move_up"):
		dir += up_direction
	if Input.is_action_pressed("move_down"):
		dir -= up_direction
	var target := dir.normalized() * fly_speed if dir.length_squared() > 1e-6 else Vector3.ZERO
	velocity = velocity.lerp(target, clampf(8.0 * delta, 0.0, 1.0))

func _update_hud() -> void:
	var editor := head.get_node_or_null("Camera3D/Editor")
	var mode := "flying %.0f m/s" % fly_speed if flying else ("walking" if is_on_floor() else "airborne")
	hud.text = "%s   |   %s\nLMB dig  RMB add  Q brush  scroll size  E smooth  1-4 material  R preview  Tab fly" % [
		mode, editor.describe() if editor and editor.has_method("describe") else ""]
