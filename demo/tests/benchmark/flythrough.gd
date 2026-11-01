extends Node
## Fly-through benchmark: the player flies a great circle ~40 m above the
## terrain at a few speeds, capped at 60 fps, while every frame's time and the
## terrain's per-stage costs (JarVoxelStats) are recorded. Prints percentiles,
## the costliest stages and the worst frames; OUT=path.csv writes every frame.
##   godot --path demo res://tests/benchmark/flythrough.tscn
## Options (environment): SCENE, SPEEDS (m/s, comma separated), SECONDS (per
## speed), ALTITUDE (m), CLIMB (m/s: rises while flying, e.g. out to space),
## OUT.

var SCENE := _env("SCENE", "res://demo/planet.tscn")
var SPEEDS := Array(_env("SPEEDS", "50,500").split(",")).map(func(s): return float(s))
var SECONDS := float(_env("SECONDS", "20"))
var ALTITUDE := float(_env("ALTITUDE", "40"))
var CLIMB := float(_env("CLIMB", "0"))
var OUT := _env("OUT", "")

var _frames: Array[Dictionary] = []

static func _env(name: String, fallback: String) -> String:
	var value := OS.get_environment(name)
	return value if value != "" else fallback

func _ready() -> void:
	Engine.max_fps = 60
	var scene: Node = load(SCENE).instantiate()
	var player: DemoPlayer = scene.get_node("Player")
	add_child(scene)
	var volume: JarVoxelVolume = scene.get_node("%Planet/JarVoxelVolume")
	var sdf: JarTerrainSdf = volume.sdf
	var start := Time.get_ticks_msec()
	while not player._spawned and Time.get_ticks_msec() - start < 60000:
		await get_tree().physics_frame
	while volume.is_building() or volume.is_meshing():
		await get_tree().process_frame
	print("spawned, terrain settled after %.1f s" % ((Time.get_ticks_msec() - start) / 1000.0))
	player.set_physics_process(false) # the benchmark moves it

	# A great circle through the player's direction, in volume space.
	var dir := (volume.global_transform.affine_inverse() * player.global_position).normalized()
	var axis := dir.cross(Vector3(0.3, 0.9, 0.1)).normalized()
	var angle := 0.0
	var altitude := ALTITUDE
	for speed in SPEEDS:
		var label := "%d m/s" % speed
		var elapsed := 0.0
		var last := Time.get_ticks_usec()
		var previous_ms := 0.0
		while elapsed < SECONDS:
			await get_tree().process_frame
			var now := Time.get_ticks_usec()
			# This resumes before the volumes' process: get_frame() is the
			# frame before last, the frame time measured last time.
			var frame_ms := previous_ms
			previous_ms = (now - last) / 1000.0
			last = now
			var delta := get_process_delta_time()
			elapsed += delta
			angle += speed * delta / sdf.radius
			altitude += CLIMB * delta
			var at := dir.rotated(axis, angle)
			var ahead := dir.rotated(axis, angle + 200.0 / sdf.radius)
			var height := maxf(sdf.sample_height(at * sdf.radius), 0.0)
			var to_global := volume.global_transform
			player.global_position = to_global * (at * (sdf.radius + height + altitude))
			var look := to_global * (ahead * (sdf.radius + maxf(sdf.sample_height(ahead * sdf.radius), 0.0)))
			player.look_at(look, to_global.basis * at)
			var stats := JarVoxelStats.get_frame()
			stats["frame"] = frame_ms
			stats["phase"] = label if CLIMB == 0.0 else "%s, %d km up" % [label, int(altitude / 10000.0) * 10]
			_frames.append(stats)
	_report()
	get_tree().quit()

func _report() -> void:
	for phase in _phases():
		var frames := _frames.filter(func(f): return f.phase == phase)
		frames = frames.slice(30) # the first frames of a phase still settle
		var times := frames.map(func(f): return f.frame)
		print("\n== %s: %d frames" % [phase, frames.size()])
		print("frame ms: p50 %.1f  p95 %.1f  p99 %.1f  max %.1f" % [
			_pct(times, 0.5), _pct(times, 0.95), _pct(times, 0.99), times.max()])
		for group in ["main", "workers"]:
			var rows := []
			for stage in frames[0][group]:
				var values := frames.map(func(f): return f[group][stage])
				var total: float = values.reduce(func(a, b): return a + b, 0.0)
				rows.append([stage, total / frames.size(), _pct(values, 0.99), values.max()])
			rows.sort_custom(func(a, b): return a[1] > b[1])
			print("%s (ms per frame: avg / p99 / max):" % group)
			for row in rows:
				if row[3] > 0.0:
					print("  %-14s %7.2f %7.2f %7.2f" % row)
		var counts := {}
		for f in frames:
			for c in f.counts:
				counts[c] = counts.get(c, 0) + f.counts[c]
		print("counts: ", counts)
		var worst := frames.duplicate()
		worst.sort_custom(func(a, b): return a.frame > b.frame)
		print("worst frames:")
		for f in worst.slice(0, 5):
			var parts := []
			for stage in f.main:
				if f.main[stage] >= 0.5:
					parts.append("%s %.1f" % [stage, f.main[stage]])
			print("  %.1f ms: %s" % [f.frame, ", ".join(parts)])
	if OUT != "":
		_write_csv()

func _phases() -> Array:
	var phases := []
	for f in _frames:
		if not phases.has(f.phase):
			phases.append(f.phase)
	return phases

static func _pct(values: Array, q: float) -> float:
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[clampi(int(q * (sorted.size() - 1)), 0, sorted.size() - 1)]

func _write_csv() -> void:
	var file := FileAccess.open(OUT, FileAccess.WRITE)
	var header := ["phase", "frame"]
	for group in ["main", "workers", "counts", "gauges"]:
		for key in _frames[0][group]:
			header.append("%s:%s" % [group, key])
	file.store_line(",".join(header))
	for f in _frames:
		var row := [f.phase, "%.3f" % f.frame]
		for group in ["main", "workers", "counts", "gauges"]:
			for key in f[group]:
				row.append(str(f[group][key]))
		file.store_line(",".join(row))
	print("wrote ", OUT)
