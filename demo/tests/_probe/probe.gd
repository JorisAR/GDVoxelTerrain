extends Node

func _ready() -> void:
	var sdf: JarTerrainSdf = load("res://demo/giant/giant_terrain.tres")
	var dir := Vector3(0.103242, -0.39792, 0.911592).normalized()
	dir = (dir * sdf.radius + dir.cross(Vector3.UP).normalized() * 1500.0).normalized()
	var t := dir.cross(Vector3.UP).normalized()
	var ground := dir * (sdf.radius + sdf.sample_height(dir * sdf.radius))
	var look_dir := (dir * sdf.radius + t * 4.0).normalized()
	var look := look_dir * (sdf.radius + sdf.sample_height(look_dir * sdf.radius))
	var eye := ground + dir * 2.5
	print("VIEW %.2f,%.2f,%.2f %.2f,%.2f,%.2f height %.1f" % [eye.x, eye.y, eye.z, look.x, look.y, look.z, sdf.sample_height(dir * sdf.radius)])
	get_tree().quit()
