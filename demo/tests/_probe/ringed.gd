extends Node
func _ready() -> void:
	var scene: Node = load("res://demo/giant_planet.tscn").instantiate()
	add_child(scene)
	await get_tree().create_timer(3.0).timeout
	var cam := get_viewport().get_camera_3d()
	var giant: JarCelestialBody = scene.get_node("Sun/RingedOrbit/RingedGiant")
	var rings: Node3D = scene.get_node("Sun/RingedOrbit/RingedGiant/Rings")
	var shell: Node3D = rings.get_child(0, true)
	var real := giant.get_center().distance_to(cam.global_position)
	var up := cam.global_position.normalized()
	print("cam far ", cam.far, " giant at ", real / 1000.0, " km, shell at ", shell.global_position.distance_to(cam.global_position) / 1000.0, " km")
	print("angular size of rings deg ", rad_to_deg(2.0 * atan(2.33 * 6.0e6 / real)))
	var planet: JarCelestialBody = scene.get_node("Sun/PlanetOrbit/Planet")
	var up2 := (cam.global_position - planet.get_center()).normalized()
	print("elevation of giant above horizon deg ", 90.0 - rad_to_deg(up2.angle_to(giant.get_center() - cam.global_position)))
	var sun: JarCelestialBody = scene.get_node("Sun")
	print("sun elevation deg ", 90.0 - rad_to_deg(up2.angle_to(sun.get_center() - cam.global_position)))
	get_tree().quit()
