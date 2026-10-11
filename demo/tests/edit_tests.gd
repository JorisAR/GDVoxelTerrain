extends Node
## Headless end-to-end checks for volume edits, verified with physics raycasts
## against the rebuilt chunk colliders. Run:
##   godot --headless --path demo res://tests/edit_tests.tscn

const SCENE := preload("res://demo/flat_terrain.tscn")

var _failures := 0
var volume: JarVoxelVolume

func _ready() -> void:
	var scene := SCENE.instantiate()
	var camera: Camera3D = scene.get_node("Camera3D")
	camera.set_script(null)
	add_child(scene)
	volume = scene.get_node("World/JarVoxelVolume")
	_run.call_deferred(camera.global_position)

func _run(at: Vector3) -> void:
	await _settle()
	var ground := _ground_height(at)
	_check("ground found", not is_nan(ground), "y=%.2f" % ground)
	if is_nan(ground):
		return _finish()

	# 1. A box rotated 45 degrees around Y, 10 m tall, centered on the ground.
	var box := JarBoxSdf.new()
	box.extent = Vector3(3, 5, 3)
	volume.edit(box, JarVoxelVolume.SDF_OPERATION_UNION, Transform3D(Basis(Vector3.UP, PI / 4), Vector3(at.x, ground, at.z)))
	await _settle()
	var top := _ground_height(at)
	_check("box raises ground", absf(top - (ground + 5.0)) < 1.0, "top=%.2f expected=%.2f" % [top, ground + 5.0])
	# (2.5, 2.5) would be inside the unrotated box, but rotated 45 degrees it lies
	# 3.54 m out along a face normal, past the 3 m half-extent.
	var side := _ground_height(at + Vector3(2.5, 0, 2.5))
	_check("box is rotated", side < top - 2.0, "side=%.2f" % side)

	# 2. Carve a sphere into the box top.
	volume.sphere_edit(Vector3(at.x, top, at.z), 2.0, false)
	await _settle()
	var pit := _ground_height(at)
	_check("sphere carves", pit < top - 1.0, "pit=%.2f" % pit)

	# 3. Stamp a prism mesh, scaled 2x, beside the box.
	var prism := PrismMesh.new()
	prism.size = Vector3(2, 2, 2)
	var mesh_sdf := JarMeshSdf.new()
	mesh_sdf.mesh = prism
	var spot := at + Vector3(12, 0, 0)
	var spot_ground := _ground_height(spot)
	volume.edit(mesh_sdf, JarVoxelVolume.SDF_OPERATION_UNION,
		Transform3D(Basis().scaled(Vector3.ONE * 2.0), Vector3(spot.x, spot_ground + 2.0, spot.z)))
	await _settle()
	var peak := _ground_height(spot)
	_check("mesh stamp", absf(peak - (spot_ground + 4.0)) < 1.0, "peak=%.2f expected=%.2f" % [peak, spot_ground + 4.0])

	# 4. Unbounded SDFs are rejected by edit() instead of editing the whole world.
	volume.edit(JarPlaneSdf.new(), JarVoxelVolume.SDF_OPERATION_UNION, Transform3D())
	_check("unbounded edit rejected", not volume.is_building())
	_finish()

func _finish() -> void:
	print("Edit tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

func _check(name: String, ok: bool, detail := "") -> void:
	if not ok:
		_failures += 1
	print("%s %s %s" % ["PASS" if ok else "FAIL", name, detail])

## Waits until the volume is idle and colliders had time to upload.
func _settle() -> void:
	var idle := 0
	while idle < 30:
		await get_tree().physics_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0

func _ground_height(at: Vector3) -> float:
	var space := volume.get_world_3d().direct_space_state
	var query := PhysicsRayQueryParameters3D.create(Vector3(at.x, 500, at.z), Vector3(at.x, -500, at.z))
	var hit := space.intersect_ray(query)
	return hit.position.y if hit else NAN
