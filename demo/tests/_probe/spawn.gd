extends Node
func _ready() -> void:
	var scene: Node = load("res://demo/giant_planet.tscn").instantiate()
	add_child(scene)
	var player: CharacterBody3D = scene.get_node("Player")
	var volume: JarVoxelVolume = scene.find_children("*", "JarVoxelVolume", true, false)[0]
	var t: JarTerrainSdf = volume.sdf
	var start := Time.get_ticks_msec()
	while Time.get_ticks_msec() - start < 30000:
		await get_tree().create_timer(5.0).timeout
		var l := volume.to_local(player.global_position)
		var column := l.normalized() * t.radius
		var ground := t.sample_height(column)
		print("player r-R=", l.length() - t.radius, " sample_height=", ground, " over=", l.length() - t.radius - ground, " on_floor=", player.is_on_floor(), " sdf=", volume.get_distance(player.global_position))
	await RenderingServer.frame_post_draw
	get_viewport().get_texture().get_image().save_png(OS.get_environment("OUT"))
	get_tree().quit()
