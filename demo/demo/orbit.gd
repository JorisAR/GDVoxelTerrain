extends Node3D
## Spins this node, so children (e.g. a moon volume) orbit and rotate with it.

@export var degrees_per_second := 2.0
@export var axis := Vector3.UP

func _process(delta: float) -> void:
	rotate(axis.normalized(), deg_to_rad(degrees_per_second) * delta)
