extends Label

func _process(delta: float) -> void:
	text = str(1 / delta)
