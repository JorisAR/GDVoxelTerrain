extends "res://tests/topology_tests.gd"

func _ready() -> void:
	await _moving_seams()
	printerr("moving done, failures ", _failures)
	get_tree().quit()
