extends Node
func _ready() -> void:
	var root := Node3D.new()
	add_child(root)
	var planet := JarCelestialBody.new()
	planet.radius = 1000.0
	root.add_child(planet)
	var air := JarBodyAtmosphere.new()
	planet.add_child(air)
	for i in 3:
		await get_tree().physics_frame
	await get_tree().process_frame
	print("contains ", air.contains(Vector3(0, 1001, 0)), " lut ", air.get_active_material().get_shader_parameter("transmittance_lut"), " body ", air.get_body(), " frame center ", planet.get_center(), " sea ", planet.get_effective_sea_level())
	get_tree().quit()
