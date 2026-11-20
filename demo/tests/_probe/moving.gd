extends Node

func _ready() -> void:
	var sdf: JarTerrainSdf = load("res://tests/planet_terrain_4km.tres")
	var volume := JarVoxelVolume.new()
	volume.chunk_scene = TestWorld.CHUNK_SCENE
	volume.extent = 10000.0
	volume.sdf = sdf
	volume.meshing_algorithm = JarVoxelVolume.MESHING_DUAL_CONTOURING
	add_child(volume)
	var up := Vector3(0.45, 0.55, 0.7).normalized()
	var axis := up.cross(Vector3.UP).normalized()
	var viewer := JarVoxelViewer.new()
	viewer.position = up * (sdf.radius + sdf.sample_height(up * sdf.radius) + 5.0)
	add_child(viewer)
	var start := Time.get_ticks_msec()
	var idle := 0
	while idle < 30 and Time.get_ticks_msec() - start < 60000:
		await get_tree().process_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0
	printerr("settled after %d ms idle=%d" % [Time.get_ticks_msec() - start, idle])
	var angle := 0.0
	var build_ms := 0.0
	var mesh_ms := 0.0
	var commits := 0
	var meshed := 0
	for frame in 1500:
		if not volume.is_building():
			angle += 8.0 / sdf.radius
		var dir := up.rotated(axis, angle)
		viewer.position = dir * (sdf.radius + sdf.sample_height(dir * sdf.radius) + 5.0)
		var t := Time.get_ticks_msec()
		await get_tree().process_frame
		var f := JarVoxelStats.get_frame()
		build_ms += f.workers["build"]
		mesh_ms += f.workers["mesh"]
		commits += f.counts["chunks committed"]
		meshed += f.counts["chunks meshed"]
		if frame % 100 == 0:
			var t2 := Time.get_ticks_msec()
			var r := Topology.analyze(volume, dir * (sdf.radius + sdf.sample_height(dir * sdf.radius)), 20.0)
			printerr("frame %d: frame %d ms, analyze %d ms, open %d tris %d, building %s meshing %s queue %s committed %d meshed %d build ms %.0f mesh ms %.0f" % [frame, t2 - t, Time.get_ticks_msec() - t2, r.open, r.triangles, volume.is_building(), volume.is_meshing(), f.gauges["mesh queue"], commits, meshed, build_ms, mesh_ms])
	printerr("done")
	get_tree().quit()
