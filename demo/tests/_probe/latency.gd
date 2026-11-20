extends Node
## Edit latency probe: frames and ms from sphere_edit() to the octree edit
## dispatched, built, meshed (shown) and the collider updated.

var SCENE := OS.get_environment("SCENE") if OS.get_environment("SCENE") != "" else "res://demo/planet.tscn"
var WALK := OS.get_environment("WALK") != ""

func _ready() -> void:
	Engine.max_fps = 60
	var scene: Node = load(SCENE).instantiate()
	var player: DemoPlayer = scene.get_node("Player")
	add_child(scene)
	var volume: JarVoxelVolume = scene.get_node("%Planet/JarVoxelVolume")
	var t0 := Time.get_ticks_msec()
	while not (player._spawned and player.is_on_floor()) and Time.get_ticks_msec() - t0 < 60000:
		await get_tree().physics_frame
	for i in 180:
		await get_tree().process_frame
	while volume.is_building() or volume.is_meshing():
		await get_tree().process_frame
	print("settled")
	if WALK:
		Input.action_press("move_forward")
	var space := volume.get_world_3d().direct_space_state
	var rows := []
	for e in 12:
		var up := player.up_direction
		var fwd := -player.global_transform.basis.z.slide(up).normalized()
		var p := player.global_position + fwd * 4.0
		var q := PhysicsRayQueryParameters3D.create(p + up * 6.0, p - up * 20.0)
		q.exclude = [player.get_rid()]
		var hit := space.intersect_ray(q)
		if not hit:
			print("no ground"); continue
		var ground: Vector3 = hit.position
		var busy := "build" if volume.is_building() else ("mesh" if volume.is_meshing() else "idle")
		var start := Time.get_ticks_usec()
		volume.sphere_edit(ground, 1.5, false)
		var f := 0
		var built := -1; var shown := -1; var coll := -1
		var built_ms := 0.0; var shown_ms := 0.0; var coll_ms := 0.0
		var dispatched := -1; var disp_ms := 0.0
		while f < 600 and coll < 0:
			await get_tree().process_frame
			f += 1
			var ms := (Time.get_ticks_usec() - start) / 1000.0
			if dispatched < 0 and volume.is_building():
				dispatched = f; disp_ms = ms
			if dispatched >= 0 and built < 0 and not volume.is_building():
				built = f; built_ms = ms
			if built >= 0 and shown < 0 and not volume.is_meshing():
				shown = f; shown_ms = ms
			var h := space.intersect_ray(q)
			if h and (h.position - ground).dot(up) < -0.5:
				coll = f; coll_ms = ms
		print("edit %2d busy=%s dispatch f%d %.1fms | built f%d %.1fms | shown f%d %.1fms | collider f%d %.1fms" % [e, busy, dispatched, disp_ms, built, built_ms, shown, shown_ms, coll, coll_ms])
		for i in 20:
			await get_tree().process_frame
	get_tree().quit()
