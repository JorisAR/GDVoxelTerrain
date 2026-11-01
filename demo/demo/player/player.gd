class_name DemoPlayer
extends CharacterBody3D
## First-person capsule character for any volume: it stands along gravity
## (JarVoxelUniverse gravity around celestial bodies, Godot's gravity
## otherwise), walks, sprints and jumps, and flies with Tab.
##
## On moving bodies (an orbiting, spinning moon) it moves along with the body
## whose gravity dominates where it is.
##
## WASD: move   Space: jump / fly up   Shift: sprint / fly down
## Tab: toggle flying   Ctrl + scroll: fly speed   M: toggle mesher
## Esc: release the mouse

@export var walk_speed := 5.0
@export var sprint_speed := 10.0
@export var jump_speed := 5.5
@export var ground_acceleration := 14.0
@export var air_acceleration := 3.0
@export var fly_speed := 20.0
## Upper limit of Ctrl + scroll (the moon is 60 km away).
@export var max_fly_speed := 20000.0
@export var mouse_sensitivity := 0.0025
## How quickly the body turns to match a new gravity direction (1/s).
@export var align_speed := 8.0
## While flying, the most it turns to follow up (degrees per second): passing
## from one body's pull into another's swings up around calmly, not at once.
@export var max_fly_turn_speed := 60.0
@export var start_flying := false

@onready var head: Node3D = $Head
@onready var hud: Label = $HUD/Label

var flying := false
var _spawned := false
var _moved_to_surface := false
var _frame: JarCelestialBody # body whose motion the player currently follows
# Fly mode steers towards fly_speed relative to the scene's frame. When the
# universe switches frames it converts the velocity; that change is kept here
# and fades out, instead of being steered away within a few frames (a jolt).
var _fly_offset := Vector3.ZERO
var _last_velocity := Vector3.ZERO
var _frame_switched := false

func _ready() -> void:
	flying = start_flying
	floor_snap_length = 0.5
	floor_max_angle = deg_to_rad(50.0)
	Input.mouse_mode = Input.MOUSE_MODE_CAPTURED
	_add_population_buttons()
	JarVoxelUniverse.frame_changed.connect(func(_anchor, _rotating): _frame_switched = true)
	JarVoxelUniverse.origin_shifted.connect(_on_origin_shifted)

## Floating origin: the scene (this body included, its velocity turned along)
## moved by `shift`.
func _on_origin_shifted(shift: Transform3D) -> void:
	_fly_offset = shift.basis * _fly_offset
	_last_velocity = shift.basis * _last_velocity

var _features_button: CheckButton
var _details_button: CheckButton

## Population on / off (G / H, or these buttons once Esc frees the mouse):
## population is the costliest part of the terrain.
func _add_population_buttons() -> void:
	var box := HBoxContainer.new()
	box.set_anchors_and_offsets_preset(Control.PRESET_TOP_RIGHT)
	box.grow_horizontal = Control.GROW_DIRECTION_BEGIN
	box.position.y = 8.0
	$HUD.add_child(box)
	_features_button = _population_button(box, "Trees & rocks (G)", func(on: bool): _set_population(on, null))
	_details_button = _population_button(box, "Grass & flowers (H)", func(on: bool): _set_population(null, on))

func _population_button(box: Control, text: String, toggled: Callable) -> CheckButton:
	var button := CheckButton.new()
	button.text = text
	button.button_pressed = true
	button.focus_mode = Control.FOCUS_NONE
	button.toggled.connect(toggled)
	box.add_child(button)
	return button

func _set_population(features: Variant, details: Variant) -> void:
	for volume: JarVoxelVolume in _volumes():
		if features != null:
			volume.population_features_enabled = features
		if details != null:
			volume.population_details_enabled = details

func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.mouse_mode == Input.MOUSE_MODE_CAPTURED:
		# Yaw turns the body around its own up axis; pitch only tilts the head.
		rotate_object_local(Vector3.UP, -event.relative.x * mouse_sensitivity)
		head.rotation.x = clampf(head.rotation.x - event.relative.y * mouse_sensitivity, -1.5, 1.5)
	elif event.is_action_pressed("ui_cancel"):
		Input.mouse_mode = Input.MOUSE_MODE_VISIBLE if Input.mouse_mode == Input.MOUSE_MODE_CAPTURED else Input.MOUSE_MODE_CAPTURED
	elif event.is_action_pressed("ui_focus_next"):
		flying = not flying
	elif event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_G:
		_features_button.button_pressed = not _features_button.button_pressed
	elif event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_H:
		_details_button.button_pressed = not _details_button.button_pressed
	elif event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_J:
		# Falling terrain on / off (JarVoxelDebris nodes under the volumes).
		for debris: JarVoxelDebris in _debris_nodes():
			debris.enabled = not debris.enabled
	elif event is InputEventKey and event.pressed and not event.echo and event.keycode == KEY_M:
		for volume: JarVoxelVolume in _volumes():
			volume.meshing_algorithm = (1 - volume.meshing_algorithm) as JarVoxelVolume.MeshingAlgorithm
	elif event is InputEventMouseButton and event.pressed and event.ctrl_pressed:
		if event.button_index == MOUSE_BUTTON_WHEEL_UP:
			fly_speed = minf(fly_speed * 1.5, max_fly_speed)
		elif event.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			fly_speed = maxf(fly_speed / 1.5, 1.0)

func _physics_process(delta: float) -> void:
	if _frame_switched:
		_frame_switched = false
		_fly_offset += velocity - _last_velocity
	_follow_frame()
	var gravity := get_gravity()
	var pull := gravity
	if JarVoxelUniverse.has_bodies():
		# Near a body: felt in the frame of the body it moves along with (see
		# _follow_frame). In free flight it moves in the scene's frame, but turns
		# by the pulls as they are, which stay smooth when the frame switches.
		var riding := _frame != null
		gravity = JarVoxelUniverse.get_gravity(global_position, _frame)
		pull = gravity if riding else JarVoxelUniverse.get_gravity(global_position, null, false)
		up_direction = JarVoxelUniverse.get_up(global_position, up_direction, _frame, riding)
	elif gravity.length_squared() > 1e-6:
		up_direction = -gravity.normalized()
	# Turn slowly where gravity is weak (between bodies), not at all without it.
	_align_to(up_direction, delta * clampf(pull.length() / 2.0, 0.0, 1.0))

	if not _spawned:
		_spawned = _try_spawn()
		return

	var input := Input.get_vector("move_left", "move_right", "move_forward", "move_backward")
	if flying:
		_fly(input, delta)
	else:
		_fly_offset = Vector3.ZERO
		_walk(input, gravity, delta)
	move_and_slide()
	_last_velocity = velocity
	_update_hud()

## Stands on the ground once the terrain around the spawn point is final.
## Colliders are only built near viewers, and a coarse (distant) collider can
## lie below the real surface, so: move to the surface the volume's SDF
## predicts, wait until every volume has finished building and meshing there,
## then stand on the physics ground.
func _try_spawn() -> bool:
	if start_flying:
		return true
	var volumes := _volumes()
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
	# From well above: the first move used the terrain as it was then (coarse,
	# tens of meters off on a large planet), which may have left it below the ground.
	var query := PhysicsRayQueryParameters3D.create(global_position + up_direction * 500.0, global_position - up_direction * 2000.0)
	query.exclude = [get_rid()]
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	if not hit:
		return false
	global_position = hit.position + up_direction * 0.1
	velocity = Vector3.ZERO
	return true

## Moves and turns the player with the body it stands on (or flies low over):
## the celestial body whose gravity dominates at the player, below that body's
## rotating frame height. Higher up the player flies freely.
func _follow_frame() -> void:
	var frame: JarCelestialBody = JarVoxelUniverse.get_dominant_body(global_position)
	if frame:
		# Only near the surface (the body's rotating frame height, a little
		# more once following): in flight the player is free.
		var height := frame.get_effective_rotating_frame_height() * (1.2 if frame == _frame else 1.0)
		if frame.get_altitude(global_position) > height:
			frame = null
	if frame != _frame:
		# Keep moving as before: the velocity becomes relative to the new body.
		velocity += _body_velocity(_frame) - _body_velocity(frame)
		_frame = frame
		# While following, its velocity is relative to the body, so the
		# universe must not convert it on frame switches.
		if frame:
			add_to_group("jar_frame_exempt")
		else:
			remove_from_group("jar_frame_exempt")
	elif frame:
		# Apply how the frame moved since the last physics frame (from the
		# universe, in doubles: differencing its global transforms here
		# rounded at its distance, centimeters of jitter on a large planet).
		var motion := JarVoxelUniverse.get_body_motion(frame)
		global_transform = motion * global_transform
		velocity = motion.basis * velocity

## How a point at the player moves along with `body` (its orbit and spin).
func _body_velocity(body: Node3D) -> Vector3:
	return JarVoxelUniverse.get_body_velocity_at(body, global_position) if body else Vector3.ZERO

var _debris: Array[Node] = []
var _debris_searched := false

## The scene's JarVoxelDebris nodes (falling terrain), found once.
func _debris_nodes() -> Array[Node]:
	if not _debris_searched:
		_debris_searched = true
		_debris = get_tree().root.find_children("*", "JarVoxelDebris", true, false)
	return _debris

var _volume_nodes: Array[Node] = []

## The scene's volumes, found once (searching the whole tree every frame is slow:
## a planet has thousands of chunk nodes).
func _volumes() -> Array[Node]:
	_volume_nodes = _volume_nodes.filter(func(v): return is_instance_valid(v))
	if _volume_nodes.is_empty():
		_volume_nodes = get_tree().root.find_children("*", "JarVoxelVolume", true, false)
	return _volume_nodes

## Rotates the body so its up axis follows gravity, keeping the heading.
func _align_to(up: Vector3, delta: float) -> void:
	var current := global_basis.y.normalized()
	if current.dot(up) > 0.99999:
		return
	var turn := Quaternion(current, up)
	var target := Basis(turn) * global_basis
	var weight := clampf(align_speed * delta, 0.0, 1.0)
	if flying:
		var angle := current.angle_to(up)
		weight = minf(weight, deg_to_rad(max_fly_turn_speed) * get_physics_process_delta_time() / angle)
	global_basis = global_basis.orthonormalized().slerp(target.orthonormalized(), weight)

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
	_fly_offset *= exp(-delta / 1.5)
	velocity = velocity.lerp(target + _fly_offset, clampf(8.0 * delta, 0.0, 1.0))

func _update_hud() -> void:
	var editor := head.get_node_or_null("Camera3D/Editor")
	var mode := "flying %.0f m/s (Ctrl+scroll)" % fly_speed if flying else ("walking" if is_on_floor() else "airborne")
	var volumes := _volumes()
	var mesher: String = ["surface nets", "dual contouring"][volumes[0].meshing_algorithm] if volumes else ""
	var brush: String = editor.describe() if editor and editor.has_method("describe") else ""
	var debris := _debris_nodes()
	var falling: String = ("   |   falling terrain %s (J)" % ("on" if debris[0].enabled else "off")) if debris else ""
	hud.text = "%d fps   %s   |   %s   |   %s (M)%s\nLMB dig  RMB add  Q brush  scroll size  E smooth  1-8 material (0 auto)  P paint  R preview  Tab fly  G trees  H grass  J falling terrain" % [
		Engine.get_frames_per_second(), mode, brush, mesher, falling]
