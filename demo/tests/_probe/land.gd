extends SceneTree
func _init() -> void:
	var t: JarTerrainSdf = load("res://demo/giant/giant_terrain.tres")
	var v := JarVoxelVolume.new()
	v.sdf = t
	pass
	var from := Vector3(298396.5, 356014.8, 381647)
	var land: Vector3 = v.find_land(from)
	var up := land.normalized()
	print("spawn ", land, " h=", land.length() - t.radius - t.sea_level, " moved ", land.distance_to(from))
	# a coast: walk from the spawn until sea
	for dir: Vector3 in [Vector3(1,0,0), Vector3(0,1,0), Vector3(0,0,1), Vector3(-1,0,0)]:
		var tang := dir - up * dir.dot(up)
		tang = tang.normalized()
		var d := 100.0
		var last := 0.0
		while d < 60000.0:
			var p := (land + tang * d).normalized() * t.radius
			var h := t.sample_height(p) - t.sea_level
			if h < 0.0:
				print("coast at ", d, " ", (land + tang * d).normalized() * (t.radius + t.sea_level + 30.0))
				break
			d += 100.0
	var hs := []
	for i in 40:
		var a := (land + Vector3(i * 4.0, 0, 0)).normalized() * t.radius
		hs.append(snappedf(t.sample_height(a), 0.1))
	print(hs)
	quit()
