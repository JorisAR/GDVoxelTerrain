extends JarVoxelTerrain

var t = 0;

func _process(delta: float) -> void:
	t += delta
	global_position.y = sin(0.2 * PI * t) * 100;
