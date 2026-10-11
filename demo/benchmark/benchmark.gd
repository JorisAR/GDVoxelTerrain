extends Node
## Headless terrain benchmark. Run with a fixed timestep so every run does the
## same work regardless of machine speed:
##   godot --headless --fixed-fps 60 --path demo res://benchmark/benchmark.tscn
## Prints one RESULT line per phase; wall-clock numbers are what to compare.
## Set CHUNK_DUMP=<file> to also write the resulting chunk set for diffing,
## FEATURES=1 to populate the planet with the demo's trees and rocks.

const CHUNK_SCENE := preload("res://addons/jar_voxel_terrain/src/chunk.tscn")
const PLANET_SDF := preload("res://demo/planet_sdf.tres")
const IDLE_TIMEOUT_MS := 180000
const FLIGHT_SECONDS := 10.0 # wall-clock: measures how well the terrain keeps up
const FLIGHT_VELOCITY := Vector3(100, 0, 0) # metres per wall-clock second
const EDIT_COUNT := 16

var terrain: JarVoxelVolume
var camera: Node3D # the viewer

## A self-contained scene (no demo dependencies): a 4 km planet and one viewer
## 100 m above its surface.
func _ready() -> void:
	var planet := Node3D.new()
	planet.position = Vector3(0, -4196, 0)
	add_child(planet)
	terrain = JarVoxelVolume.new()
	terrain.chunk_scene = CHUNK_SCENE
	terrain.sdf = PLANET_SDF
	terrain.up_mode = JarVoxelVolume.UP_MODE_RADIAL
	terrain.sea_level = 4096.0
	terrain.lod_automatic_update_distance = 32.0
	_configure_size(terrain)
	if OS.get_environment("MESHER") == "dc":
		terrain.meshing_algorithm = JarVoxelVolume.MESHING_DUAL_CONTOURING
	terrain.manifold = OS.get_environment("MANIFOLD") == "1"
	if OS.get_environment("FEATURES") == "1":
		terrain.population_terrain_features = [load("res://demo/assets/population/features/pine.tres"),
			load("res://demo/assets/population/features/rock.tres")]
	planet.add_child(terrain)
	camera = JarVoxelViewer.new()
	camera.position = Vector3(-5, 20, 200)
	add_child(camera)
	_run.call_deferred()

## Octree covering 2^20 m at 1 m voxels.
func _configure_size(volume: JarVoxelVolume) -> void:
	volume.extent = 1048576.0

func _run() -> void:
	await _measure_until_idle("initial_build")
	_report_memory()
	await _measure_flight()
	await _measure_until_idle("flight_settle")
	_dump_chunks(OS.get_environment("CHUNK_DUMP"))
	await _measure_edits()
	get_tree().quit()

## Writes every visible chunk (position + vertex count), sorted, so two builds
## can be diffed to check an optimisation did not change the output.
func _dump_chunks(path: String) -> void:
	if path.is_empty():
		return
	var lines := PackedStringArray()
	for c in terrain.get_children():
		if c is JarVoxelChunk and c.visible:
			lines.append("%s verts=%d" % [c.position.snapped(Vector3.ONE * 0.01), c.get_mesh_instance().mesh.surface_get_array_len(0)])
	lines.sort()
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string("\n".join(lines))

func _is_idle() -> bool:
	return not terrain.is_building() and not terrain.is_meshing()

func _measure_until_idle(label: String) -> void:
	var start := Time.get_ticks_usec()
	var frames := 0
	var worst := 0
	var idle_frames := 0
	var last := start
	# Several consecutive idle frames: collider updates and LOD re-checks trail meshing.
	while idle_frames < 5:
		await get_tree().process_frame
		var now := Time.get_ticks_usec()
		worst = max(worst, now - last)
		last = now
		frames += 1
		idle_frames = idle_frames + 1 if _is_idle() else 0
		if (now - start) / 1000 > IDLE_TIMEOUT_MS:
			print("RESULT %s TIMEOUT" % label)
			return
	_report(label, Time.get_ticks_usec() - start, frames, worst)

func _measure_flight() -> void:
	var start := Time.get_ticks_usec()
	var worst := 0
	var slow := 0
	var frames := 0
	var last := start
	while (last - start) < FLIGHT_SECONDS * 1000000.0:
		await get_tree().process_frame
		var now := Time.get_ticks_usec()
		var dt := now - last
		camera.position += FLIGHT_VELOCITY * (dt / 1000000.0)
		worst = max(worst, dt)
		if dt > 16667:
			slow += 1
		frames += 1
		last = now
	_report("flight", Time.get_ticks_usec() - start, frames, worst, "frames_over_16ms=%d" % slow)

func _measure_edits() -> void:
	var sdf := terrain.sdf
	var origin := terrain.global_position
	for i in EDIT_COUNT:
		# March straight down from above the camera to the surface.
		var p := camera.global_position + Vector3(i * 6.0, 0, 0)
		for step in 400:
			if sdf.sample(p - origin) < 0.0:
				break
			p.y -= 1.0
		terrain.sphere_edit(p, 6.0, i % 2 == 0)
	await _measure_until_idle("edits")

func _report(label: String, usec: int, frames: int, worst: int, extra := "") -> void:
	var chunks := 0
	for c in terrain.get_children():
		if c is JarVoxelChunk and c.visible:
			chunks += 1
	if not terrain.population_terrain_features.is_empty():
		extra += " features=%d" % terrain.get_features_near(camera.global_position, 2000.0).size()
	print("RESULT %s ms=%.1f frames=%d worst_frame_ms=%.1f chunks=%d %s" % [
		label, usec / 1000.0, frames, worst / 1000.0, chunks, extra])

## Private memory of the whole process (Windows only; the octree is allocated
## outside Godot's allocator, so Godot's own memory monitors miss it).
func _report_memory() -> void:
	if OS.get_name() != "Windows":
		return
	var out := []
	OS.execute("powershell", ["-NoProfile", "-Command", "(Get-Process -Id %d).PrivateMemorySize64" % OS.get_process_id()], out)
	if out.size() > 0 and out[0].strip_edges().is_valid_int():
		print("RESULT memory private_mb=%.0f" % (int(out[0].strip_edges()) / 1048576.0))
