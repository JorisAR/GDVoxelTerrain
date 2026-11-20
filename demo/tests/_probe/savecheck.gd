extends Node
func _ready() -> void:
	var scene: Node = load("res://demo/solar_system.tscn").instantiate()
	add_child(scene)
	for i in 120:
		await get_tree().process_frame
	_own(scene, scene)
	var packed := PackedScene.new()
	packed.pack(scene)
	var path := OS.get_environment("TEMP").path_join("claude/savecheck.tscn")
	ResourceSaver.save(packed, path)
	var text := FileAccess.get_file_as_string(path)
	print("SAVED ", text.length(), " planet_center ", text.count("planet_center"), " ring_profile ", text.count("ring_profile"), " cloud_noise ", text.count("cloud_noise"), " Image ", text.count("type=\"Image\""))
	get_tree().quit()

func _own(node: Node, owner_node: Node) -> void:
	for c in node.get_children():
		if c.owner == null and not c.get_scene_file_path():
			pass
		_own(c, owner_node)
