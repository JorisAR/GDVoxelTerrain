extends Node
## Headless checks for JarVoxelUniverse: gravity from celestial bodies (sum,
## anchor correction, sharpened up), scripted orbits and spin as functions of
## time, frame switching (the anchor stands still, exact round trips, free
## bodies keep moving), the physics space's default gravity, and bodies with
## and without volumes. Run:
##   godot --headless --path demo res://tests/universe_tests.tscn

var _failures := 0

func _ready() -> void:
	await _test_single_body()
	await _test_two_bodies()
	await _test_orbit_and_spin()
	await _test_frame_switching()
	await _test_frame_switching_off()
	await _test_rotating_frame()
	await _test_star_light()
	await _test_atmosphere()
	await _test_rigid_body_falls()
	await _test_flat_body()
	await _test_volume_body()
	await _test_space_gravity_restored()
	await _test_space_round_trip()
	print("Universe tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

func _check(name: String, ok: bool, detail := "") -> void:
	if not ok:
		_failures += 1
	print("%s %s %s" % ["PASS" if ok else "FAIL", name, detail])

func _near(a: Vector3, b: Vector3, tolerance := 1e-3) -> bool:
	return a.distance_to(b) <= tolerance * maxf(1.0, b.length())

## A body without a volume, added under `parent` at `at`.
func _body(parent: Node, at: Vector3, radius: float, gravity: float) -> JarCelestialBody:
	var body := JarCelestialBody.new()
	body.radius = radius
	body.surface_gravity = gravity
	body.position = at
	parent.add_child(body)
	return body

func _frames(count: int) -> void:
	for i in count:
		await get_tree().physics_frame

## Gravity of one body: inverse square outside, linear to zero inside.
func _test_single_body() -> void:
	var root := Node3D.new()
	add_child(root)
	var body := _body(root, Vector3.ZERO, 100.0, 10.0)
	await _frames(2)
	_check("has bodies", JarVoxelUniverse.has_bodies())
	_check("anchor is the only body", JarVoxelUniverse.get_anchor() == body)
	var outside := JarVoxelUniverse.get_gravity(Vector3(0, 200, 0))
	_check("inverse square outside", _near(outside, Vector3(0, -2.5, 0)), str(outside))
	var inside := JarVoxelUniverse.get_gravity(Vector3(50, 0, 0))
	_check("linear inside", _near(inside, Vector3(-5, 0, 0)), str(inside))
	var up := JarVoxelUniverse.get_up(Vector3(0, 0, 300), Vector3.UP)
	_check("up away from the center", _near(up, Vector3(0, 0, 1)), str(up))
	_check("up fallback at the center", JarVoxelUniverse.get_up(Vector3.ZERO, Vector3.RIGHT) == Vector3.RIGHT)
	_check("body gravity matches", _near(body.get_gravity_at(Vector3(0, 200, 0)), outside))
	root.queue_free()
	await _frames(2)

## A planet and a small moon: physical strength is the plain sum minus what
## the anchor falls with; up follows the locally dominant body.
func _test_two_bodies() -> void:
	var root := Node3D.new()
	add_child(root)
	var planet := _body(root, Vector3.ZERO, 1000.0, 10.0)
	var moon := _body(root, Vector3(3000, 0, 0), 100.0, 3.0)
	JarVoxelUniverse.set_anchor_override(planet)
	await _frames(2)
	_check("anchor override", JarVoxelUniverse.get_anchor() == planet)
	# At the planet's center: its own pull is zero, the moon's is subtracted.
	var at_center := JarVoxelUniverse.get_gravity(Vector3.ZERO)
	_check("anchor center is in free fall", at_center.length() < 1e-4, str(at_center))
	var p := Vector3(0, 1500, 0)
	var expected := planet.get_gravity_at(p) + moon.get_gravity_at(p) - moon.get_gravity_at(planet.get_center())
	var got := JarVoxelUniverse.get_gravity(p)
	_check("sum with anchor correction", _near(got, expected), "%s vs %s" % [got, expected])
	var on_moon := Vector3(3000, 100, 0)
	_check("dominant body on the moon", JarVoxelUniverse.get_dominant_body(on_moon) == moon)
	_check("dominant body near the planet", JarVoxelUniverse.get_dominant_body(Vector3(0, 1100, 0)) == planet)
	# Between the bodies, the sharpened blend leans to the stronger pull more
	# than the plain sum does.
	var between := Vector3(2200, 600, 0)
	var g_planet := planet.get_gravity_at(between)
	var g_moon := moon.get_gravity_at(between) - moon.get_gravity_at(planet.get_center())
	var plain := -(g_planet + g_moon).normalized()
	var sharp := JarVoxelUniverse.get_up(between, Vector3.UP)
	var dominant := -(g_planet if g_planet.length() > g_moon.length() else g_moon).normalized()
	_check("sharpened up leans to the dominant pull", sharp.angle_to(dominant) < plain.angle_to(dominant),
		"%.2f deg vs plain %.2f deg" % [rad_to_deg(sharp.angle_to(dominant)), rad_to_deg(plain.angle_to(dominant))])
	# Standing on the moon (the anchor there): the planet only adds its tide,
	# so up is radial to the moon although the planet pulls 1.1 m/s2 there.
	JarVoxelUniverse.set_anchor_override(moon)
	var up := JarVoxelUniverse.get_up(on_moon, Vector3.UP)
	var raw := -(planet.get_gravity_at(on_moon) + moon.get_gravity_at(on_moon)).normalized()
	_check("up on the moon is radial", rad_to_deg(up.angle_to(Vector3.UP)) < 0.5,
		"%.3f deg (raw pulls %.2f deg)" % [rad_to_deg(up.angle_to(Vector3.UP)), rad_to_deg(raw.angle_to(Vector3.UP))])
	var g := JarVoxelUniverse.get_gravity(on_moon)
	_check("gravity on the moon matches up", rad_to_deg((-g).angle_to(up)) < 0.5, "%.3f deg" % rad_to_deg((-g).angle_to(up)))
	JarVoxelUniverse.set_anchor_override(null)
	_check("override cleared", JarVoxelUniverse.get_anchor_override() == null)
	root.queue_free()
	await _frames(2)

## Orbits and spin are functions of universe time: the same tick gives the
## same placement, however it was reached.
func _test_orbit_and_spin() -> void:
	var root := Node3D.new()
	add_child(root)
	var planet := _body(root, Vector3(10, 20, 30), 500.0, 10.0)
	var orbit := JarBodyOrbit.new()
	orbit.radius = 2000.0
	orbit.period = 100.0
	orbit.axis = Vector3(0.2, 1, 0)
	orbit.phase = 30.0
	planet.add_child(orbit)
	var moon := JarCelestialBody.new()
	moon.radius = 50.0
	moon.spin_period = 10.0
	moon.spin_axis = Vector3(0, 1, 0)
	orbit.add_child(moon)
	await _frames(2)
	var ticks := 12345
	JarVoxelUniverse.set_ticks(ticks)
	await _frames(1)
	var time := JarVoxelUniverse.get_time()
	var expected_time := float(ticks + 1) / Engine.physics_ticks_per_second
	_check("time from ticks", is_equal_approx(time, expected_time), "%f vs %f" % [time, expected_time])
	var position := moon.global_position
	var expected := planet.get_center() + orbit.offset_at(time)
	_check("orbit position from time", _near(position, expected), "%s vs %s" % [position, expected])
	_check("orbit radius", is_equal_approx(position.distance_to(planet.get_center()), 2000.0))
	_check("orbit plane", absf((position - planet.get_center()).dot(orbit.axis.normalized())) < 0.05)
	var angle := fmod(time / 10.0, 1.0) * TAU
	var spin_x := Vector3(cos(angle), 0, -sin(angle))
	_check("spin from time", _near(moon.global_basis.x, spin_x), "%s vs %s" % [moon.global_basis.x, spin_x])
	var basis := moon.global_basis
	# Run on, then go back to the same tick: identical placement.
	await _frames(30)
	_check("orbit moves", moon.global_position.distance_to(position) > 1.0)
	JarVoxelUniverse.set_ticks(ticks)
	await _frames(1)
	_check("orbit is deterministic", moon.global_position == position, str(moon.global_position - position))
	_check("spin is deterministic", moon.global_basis.is_equal_approx(basis))
	# Far in the future, angles stay exact (computed in doubles, wrapped).
	JarVoxelUniverse.set_ticks(60 * 60 * 24 * 365 * 10) # ten years at 60 Hz
	await _frames(1)
	var late := JarVoxelUniverse.get_time()
	_check("orbit exact after years", _near(moon.global_position, planet.get_center() + orbit.offset_at(late)))
	JarVoxelUniverse.set_ticks(0)
	root.queue_free()
	await _frames(2)

## Same pose, to float precision at these distances (km).
func _same_pose(a: Transform3D, b: Transform3D, tolerance := 0.01) -> bool:
	return a.origin.distance_to(b.origin) < tolerance and a.basis.is_equal_approx(b.basis)

## A spinning planet with a spinning moon on an orbit, for frame switching.
func _system(root: Node3D) -> Dictionary:
	var planet := _body(root, Vector3(100, -50, 20), 1000.0, 10.0)
	planet.spin_period = 100.0
	planet.spin_axis = Vector3(0.1, 1, 0)
	var orbit := JarBodyOrbit.new()
	orbit.radius = 3000.0
	orbit.period = 200.0
	orbit.axis = Vector3(0, 1, 0.3)
	planet.add_child(orbit)
	var moon := JarCelestialBody.new()
	moon.radius = 100.0
	moon.surface_gravity = 3.0
	moon.spin_period = 50.0
	moon.spin_axis = Vector3(0.3, 1, 0)
	orbit.add_child(moon)
	return {planet = planet, orbit = orbit, moon = moon}

## The anchor stands still and the rest moves around it; switching anchors
## moves nothing at that moment, round trips are exact, and free physics
## bodies keep moving as they did, now relative to the new anchor.
func _test_frame_switching() -> void:
	var root := Node3D.new()
	add_child(root)
	var s := _system(root)
	var planet: JarCelestialBody = s.planet
	var moon: JarCelestialBody = s.moon
	var switches := []
	var on_switch := func(previous, current): switches.append([previous, current])
	JarVoxelUniverse.anchor_changed.connect(on_switch)
	JarVoxelUniverse.set_anchor_override(planet)
	await _frames(2)
	_check("frame anchor follows the anchor", JarVoxelUniverse.get_frame_anchor() == planet)
	var planet_pose := planet.global_transform
	var moon_start := moon.global_position
	await _frames(30)
	_check("anchor stands still although it spins", planet.global_transform == planet_pose)
	# Where it was placed (turned only by its spin until it became the anchor).
	_check("planet at its authored place", planet_pose.origin.is_equal_approx(Vector3(100, -50, 20)))
	_check("moon orbits the anchor", moon.global_position.distance_to(moon_start) > 10.0
		and is_equal_approx(moon.global_position.distance_to(planet.get_center()), 3000.0))
	# A free body moving with the moon (no gravity), and characters.
	var probe_at := moon.get_center() + Vector3(0, 100, 0)
	var probe := RigidBody3D.new()
	probe.gravity_scale = 0.0
	probe.linear_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	probe.angular_damp_mode = RigidBody3D.DAMP_MODE_REPLACE
	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	probe.add_child(shape)
	probe.position = probe_at
	root.add_child(probe)
	var walker := CharacterBody3D.new()
	var exempt := CharacterBody3D.new()
	exempt.add_to_group("jar_frame_exempt")
	walker.position = probe_at # carried as the probe is
	root.add_child(walker)
	root.add_child(exempt)
	await _frames(1)
	var carried := JarVoxelUniverse.get_body_velocity_at(moon, probe.global_position)
	probe.linear_velocity = carried
	probe.angular_velocity = JarVoxelUniverse.get_body_angular_velocity(moon)
	walker.velocity = carried + Vector3(1, 0, 0)
	exempt.velocity = Vector3(2, 0, 0)
	_check("moon carries points at orbit speed", carried.length() > 50.0, "%.1f m/s" % carried.length())
	await _frames(1)

	# Switch to the moon at a known tick, after recording where the moon
	# would be at the next tick with the planet still the anchor.
	var tick: int = JarVoxelUniverse.get_ticks()
	JarVoxelUniverse.set_ticks(tick)
	await _frames(1)
	var moon_next := moon.global_transform
	var relative := planet.global_transform.affine_inverse() * moon.global_transform
	JarVoxelUniverse.set_ticks(tick)
	JarVoxelUniverse.set_anchor_override(moon)
	await _frames(1)
	_check("switch signal", switches.size() == 2 and switches[0][0] == null and switches[0][1] == planet
		and switches[1][0] == planet and switches[1][1] == moon, str(switches))
	_check("new anchor stops where it was", _same_pose(moon.global_transform, moon_next),
		"%.4f m" % moon.global_position.distance_to(moon_next.origin))
	var relative_after := planet.global_transform.affine_inverse() * moon.global_transform
	_check("bodies keep their relative pose", _same_pose(relative_after, relative))
	_check("free body now at rest", probe.linear_velocity.length() < 0.02 * carried.length()
		and probe.angular_velocity.length() < 1e-3, "%.3f m/s, %.5f rad/s" % [probe.linear_velocity.length(), probe.angular_velocity.length()])
	_check("character converted", walker.velocity.distance_to(Vector3(1, 0, 0)) < 0.02 * carried.length(), str(walker.velocity))
	_check("exempt character untouched", exempt.velocity == Vector3(2, 0, 0))
	var moon_pose := moon.global_transform
	var probe_local := moon.global_transform.affine_inverse() * probe.global_position
	await _frames(30)
	_check("new anchor stands still", moon.global_transform == moon_pose, "%s vs %s" % [moon.global_transform, moon_pose])
	_check("planet moves now", planet.global_transform != planet_pose)
	_check("free body stays with the moon", (moon.global_transform.affine_inverse() * probe.global_position).distance_to(probe_local) < 0.5)

	# And back at the same tick: the planet returns exactly to its pose.
	JarVoxelUniverse.set_ticks(tick)
	JarVoxelUniverse.set_anchor_override(planet)
	await _frames(1)
	_check("round trip is exact", _same_pose(planet.global_transform, planet_pose, 1e-3),
		"%.6f m" % planet.global_position.distance_to(planet_pose.origin))
	_check("round trip moon", _same_pose(moon.global_transform, moon_next, 1e-2))

	# Many switches back and forth over a long time stay consistent with
	# the universe (relative poses are recomputed, not accumulated).
	var drift := 0.0
	for i in 20:
		JarVoxelUniverse.set_ticks(tick + i * 100003)
		JarVoxelUniverse.set_anchor_override(moon if i % 2 == 0 else planet)
		await _frames(1)
		var rel := planet.global_transform.affine_inverse() * moon.global_transform
		JarVoxelUniverse.set_ticks(tick + i * 100003)
		JarVoxelUniverse.set_anchor_override(planet if i % 2 == 0 else moon)
		await _frames(1)
		var rel2 := planet.global_transform.affine_inverse() * moon.global_transform
		drift = maxf(drift, rel.origin.distance_to(rel2.origin))
	_check("no drift across switches", drift < 0.01, "%.5f m" % drift)

	JarVoxelUniverse.anchor_changed.disconnect(on_switch)
	JarVoxelUniverse.set_anchor_override(null)
	JarVoxelUniverse.set_ticks(0)
	root.queue_free()
	await _frames(2)

## With frame switching off, the anchor body moves (spins) in the scene.
func _test_frame_switching_off() -> void:
	ProjectSettings.set_setting("jar_voxel_terrain/universe/frame_switching", false)
	var root := Node3D.new()
	add_child(root)
	var s := _system(root)
	var planet: JarCelestialBody = s.planet
	JarVoxelUniverse.set_anchor_override(planet)
	await _frames(2)
	var basis := planet.global_basis
	await _frames(30)
	_check("frame switching off: anchor spins", not planet.global_basis.is_equal_approx(basis))
	_check("frame switching off: no frame anchor", JarVoxelUniverse.get_frame_anchor() == null)
	ProjectSettings.set_setting("jar_voxel_terrain/universe/frame_switching", true)
	JarVoxelUniverse.set_anchor_override(null)
	JarVoxelUniverse.set_ticks(0)
	root.queue_free()
	await _frames(2)

## Near the anchor's surface the scene turns with it (the ground stands still,
## the sky and sun turn); higher up only its center stands still (the sky
## stands still, the body turns below). Switching between them keeps things
## moving as they did.
func _test_rotating_frame() -> void:
	var root := Node3D.new()
	add_child(root)
	var env := WorldEnvironment.new()
	env.environment = Environment.new()
	env.environment.sky_rotation = Vector3(0.1, 0.2, 0.0)
	root.add_child(env)
	var sun := DirectionalLight3D.new()
	sun.rotation = Vector3(-0.5, 0.3, 0.0)
	root.add_child(sun)
	var sun_basis := sun.global_basis
	var planet := _body(root, Vector3.ZERO, 1000.0, 10.0)
	planet.spin_period = 100.0
	var clouds := JarBodyClouds.new()
	planet.add_child(clouds)
	var viewer := JarVoxelViewer.new()
	viewer.position = Vector3(0, 1100, 0) # 100 m up; auto height: half the radius
	root.add_child(viewer)
	await _frames(2)
	_check("auto rotating frame height", is_equal_approx(planet.get_effective_rotating_frame_height(), 500.0))
	_check("rotating near the surface", JarVoxelUniverse.get_frame_anchor() == planet and JarVoxelUniverse.is_frame_rotating())
	var ground := planet.global_transform
	var sky := env.environment.sky_rotation
	var light := sun.global_basis
	await _frames(30)
	_check("rotating: ground stands still", planet.global_transform == ground)
	_check("rotating: sky turns", not env.environment.sky_rotation.is_equal_approx(sky))
	_check("rotating: sun turns", not sun.global_basis.is_equal_approx(light))
	# A body resting on the ground (at rest in the rotating frame).
	var probe := RigidBody3D.new()
	probe.gravity_scale = 0.0
	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	probe.add_child(shape)
	probe.position = Vector3(1100, 0, 0)
	root.add_child(probe)
	await _frames(1)

	viewer.position = Vector3(0, 3000, 0)
	await _frames(1)
	_check("not rotating high up", JarVoxelUniverse.get_frame_anchor() == planet and not JarVoxelUniverse.is_frame_rotating())
	# The ground moves at 2 pi r / period now (r = 1100 m).
	var surface_speed := TAU * 1100.0 / 100.0
	_check("resting body takes the ground's speed", absf(probe.linear_velocity.length() - surface_speed) < 0.5,
		"%.2f m/s (expect %.2f)" % [probe.linear_velocity.length(), surface_speed])
	var center := planet.global_position
	ground = planet.global_transform
	sky = env.environment.sky_rotation
	light = sun.global_basis
	await _frames(30)
	_check("not rotating: center stands still", planet.global_position.is_equal_approx(center))
	_check("not rotating: body turns", not planet.global_basis.is_equal_approx(ground.basis))
	_check("not rotating: sky stands still", env.environment.sky_rotation.is_equal_approx(sky))
	_check("not rotating: sun stands still", sun.global_basis.is_equal_approx(light))
	var material: ShaderMaterial = clouds.get_active_material()
	var to_body: Basis = material.get_shader_parameter("world_to_body")
	_check("clouds turn with the body", to_body.is_equal_approx(planet.global_basis.orthonormalized().transposed()))

	# From the planet's center: switching to a turning frame aligns the scene
	# with the planet (floating origin), which moves and turns it.
	viewer.position = planet.global_position + Vector3(0, 1550, 0) # 550 m: above the height, below 1.2x
	await _frames(2)
	_check("hysteresis: stays not rotating", not JarVoxelUniverse.is_frame_rotating())
	viewer.position = planet.global_position + Vector3(0, 1400, 0)
	await _frames(2)
	_check("rotating again below the height", JarVoxelUniverse.is_frame_rotating())
	viewer.position = planet.global_position + Vector3(0, 1550, 0)
	await _frames(2)
	_check("hysteresis: stays rotating", JarVoxelUniverse.is_frame_rotating())

	var environment := env.environment
	root.remove_child(planet)
	planet.queue_free()
	await _frames(2)
	_check("sky restored without bodies", environment.sky_rotation.is_equal_approx(Vector3(0.1, 0.2, 0.0)), str(environment.sky_rotation))
	_check("sun restored without bodies", sun.global_basis.is_equal_approx(sun_basis))
	root.queue_free()
	await _frames(2)

## A star (JarBodyLight) aims its light from its center at the camera, with
## its real angular size, and the universe leaves that light to it.
func _test_star_light() -> void:
	var root := Node3D.new()
	add_child(root)
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.make_current()
	var planet := _body(root, Vector3(0, -1000, 0), 900.0, 10.0)
	planet.spin_period = 60.0
	var star := _body(root, Vector3(0, 0, 10000), 100.0, 1.0)
	var light := DirectionalLight3D.new()
	root.add_child(light)
	var shine := JarBodyLight.new()
	shine.sun = light
	shine.physical_size = true
	star.add_child(shine)
	await _frames(3)
	await get_tree().process_frame
	# (The light updates every frame, the star every physics tick: a tick apart.)
	_check("star light shines from the star at the camera", rad_to_deg(light.global_basis.z.angle_to(Vector3(0, 0, 1))) < 0.5, str(light.global_basis.z))
	var size := rad_to_deg(2.0 * asin(100.0 / 10000.0))
	_check("star light angular size", is_equal_approx(light.light_angular_distance, size), "%.4f vs %.4f" % [light.light_angular_distance, size])
	_check("star light direction query", shine.get_direction_at(Vector3.ZERO).is_equal_approx((star.get_center() - Vector3.ZERO).normalized()))
	_check("star light in its group", light.is_in_group("jar_body_light"))
	# The planet (anchor, rotating near its surface) turns the universe around:
	# the star moves and its light follows it, not the sky rotation.
	await _frames(60 * 15) # a quarter of a day
	await get_tree().process_frame
	var towards := (star.get_center() - camera.global_position).normalized()
	_check("star light follows the star", rad_to_deg(light.global_basis.z.angle_to(towards)) < 0.5 and towards.z < 0.5,
		"%s vs %s" % [light.global_basis.z, towards])
	# A moon reflecting it: full when the star is behind the viewer, dark
	# when the star is behind the moon, its light as strong as its phase.
	var moon := _body(root, Vector3(0, 0, -2000), 50.0, 1.0)
	var moon_light := DirectionalLight3D.new()
	root.add_child(moon_light)
	var reflect := JarBodyLight.new()
	reflect.sun = moon_light
	reflect.reflects = shine
	reflect.energy = 0.1
	moon.add_child(reflect)
	await _frames(1)
	await get_tree().process_frame
	var to_star := star.get_center() - moon.get_center()
	var full := reflect.get_phase_at(moon.get_center() + to_star.normalized() * 500.0)
	var new_moon := reflect.get_phase_at(moon.get_center() - to_star.normalized() * 500.0)
	var half := reflect.get_phase_at(moon.get_center() + to_star.cross(Vector3.UP).normalized() * 500.0)
	_check("moon phases", is_equal_approx(full, 1.0) and new_moon < 1e-3 and absf(half - 1.0 / PI) < 0.01,
		"full %.3f half %.3f new %.3f" % [full, half, new_moon])
	_check("moonlight follows the phase", is_equal_approx(moon_light.light_energy, 0.1 * reflect.get_phase_at(camera.global_position)))
	_check("moonlight draws no disk", moon_light.sky_mode == DirectionalLight3D.SKY_MODE_LIGHT_ONLY
		and moon_light.is_in_group("jar_moon_light") and not moon_light.is_in_group("jar_star_light"))
	root.queue_free()
	await _frames(2)

## The atmosphere's model: sunlight overhead is bright and nearly white,
## reddens towards the horizon, is gone behind the planet; none of it applies
## above the air.
func _test_atmosphere() -> void:
	var root := Node3D.new()
	add_child(root)
	var planet := _body(root, Vector3.ZERO, 1000.0, 10.0)
	var air := JarBodyAtmosphere.new()
	planet.add_child(air)
	await _frames(2)
	await get_tree().process_frame
	var ground := Vector3(0, 1001, 0)
	var noon := air.get_sunlight_at(ground, Vector3.UP)
	var low := air.get_sunlight_at(ground, Vector3(1, 0.03, 0).normalized())
	var behind := air.get_sunlight_at(ground, Vector3(0.2, -1, 0).normalized())
	_check("atmosphere contains the ground", air.contains(ground) and not air.contains(Vector3(0, 2000, 0)))
	_check("noon sunlight is bright", noon.r > 0.5 and noon.b > 0.3, str(noon))
	_check("low sun reddens", low.r > low.g and low.g > low.b and low.b < noon.b * 0.5, str(low))
	_check("no sunlight behind the planet", behind.r < 1e-3, str(behind))
	_check("no tint above the air", air.get_sunlight_at(Vector3(0, 5000, 0), Vector3.RIGHT).is_equal_approx(Color(1, 1, 1)))
	var material: ShaderMaterial = air.get_active_material()
	_check("atmosphere tables built", material.get_shader_parameter("transmittance_lut") is ImageTexture
		and material.get_shader_parameter("multiscattering_lut") is ImageTexture)
	root.queue_free()
	await _frames(2)

## A plain RigidBody3D falls toward the anchor body with no setup.
func _test_rigid_body_falls() -> void:
	var root := Node3D.new()
	add_child(root)
	var body := _body(root, Vector3(0, -500, 0), 100.0, 10.0)
	var ball := RigidBody3D.new()
	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	ball.add_child(shape)
	ball.position = Vector3(160, -380, 0) # 200 m from the center: 2.5 m/s2
	root.add_child(ball)
	await _frames(31)
	var v := ball.linear_velocity
	var to_center := (body.get_center() - ball.global_position).normalized()
	var angle := rad_to_deg(v.angle_to(to_center))
	_check("rigid body falls to the body", absf(v.length() - 1.25) < 0.1 and angle < 2.0,
		"speed %.3f (expect 1.25), %.2f deg off" % [v.length(), angle])
	root.queue_free()
	await _frames(2)

## A flat body (a plane): gravity along its axis, constant with height.
func _test_flat_body() -> void:
	var root := Node3D.new()
	add_child(root)
	var ground := JarCelestialBody.new()
	ground.shape = JarCelestialBody.SHAPE_PLANE
	ground.surface_gravity = 9.8
	root.add_child(ground)
	await _frames(2)
	var g1 := JarVoxelUniverse.get_gravity(Vector3(100, 5, -40))
	var g2 := JarVoxelUniverse.get_gravity(Vector3(-900, 800, 3))
	_check("flat gravity along the axis", _near(g1, Vector3(0, -9.8, 0)) and _near(g2, g1), "%s %s" % [g1, g2])
	_check("flat up", JarVoxelUniverse.get_up(Vector3(5, 5, 5), Vector3.RIGHT) == Vector3.UP)
	var ball := RigidBody3D.new()
	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	ball.add_child(shape)
	root.add_child(ball)
	await _frames(31)
	_check("flat rigid body falls", _near(ball.linear_velocity, Vector3(0, -4.9, 0), 0.03), str(ball.linear_velocity))
	root.queue_free()
	await _frames(2)

## A body with a terrain volume takes its frame from the volume.
func _test_volume_body() -> void:
	var body := JarCelestialBody.new()
	add_child(body)
	var world := TestWorld.flat(body, Vector3(0, 10, 0))
	await _frames(2)
	_check("finds its volume", body.get_terrain() == world.volume)
	var up := body.get_up_at(Vector3(300, 50, -20))
	_check("flat volume frame", _near(up, world.volume.global_basis.y.normalized()), str(up))
	var g := JarVoxelUniverse.get_gravity(Vector3(0, 50, 0))
	_check("flat volume gravity", _near(g, -up * body.surface_gravity), str(g))
	body.queue_free()
	await _frames(2)

## With every body gone, the space falls back to the project's gravity.
func _test_space_gravity_restored() -> void:
	_check("no bodies left", not JarVoxelUniverse.has_bodies())
	var ball := RigidBody3D.new()
	var shape := CollisionShape3D.new()
	shape.shape = SphereShape3D.new()
	ball.add_child(shape)
	add_child(ball)
	ball.global_position = Vector3(300, 300, 300)
	await _frames(31)
	var g := float(ProjectSettings.get_setting("physics/3d/default_gravity"))
	var expected := Vector3(ProjectSettings.get_setting("physics/3d/default_gravity_vector")) * g * 0.5
	_check("project gravity restored", _near(ball.linear_velocity, expected, 0.03), "%s vs %s" % [ball.linear_velocity, expected])
	_check("universe gravity is project gravity", _near(JarVoxelUniverse.get_gravity(Vector3.ZERO), expected * 2.0))
	ball.queue_free()

## Out to space and back over the demo planet: off it the scene follows
## another body, so the planet moves and turns while chunks are parked in
## the pool. Reused, they must sit in the volume again (a parked chunk came
## back with the turn it had when parked: all of them, after a round trip),
## and the ground must be solid again.
func _test_space_round_trip() -> void:
	Engine.max_fps = 60
	var scene: Node = load("res://demo/planet.tscn").instantiate()
	var player: DemoPlayer = scene.get_node("Player")
	add_child(scene)
	var volume: JarVoxelVolume = scene.get_node("%Planet/JarVoxelVolume")
	var sdf: JarTerrainSdf = volume.sdf
	while not player._spawned:
		await get_tree().physics_frame
	player.set_physics_process(false)
	var dir := (volume.global_transform.affine_inverse() * player.global_position).normalized()
	var rng := RandomNumberGenerator.new()
	rng.seed = 3
	while sdf.sample_height(dir * sdf.radius) < 30.0:
		dir = (dir + Vector3(rng.randf_range(-1, 1), rng.randf_range(-1, 1), rng.randf_range(-1, 1)) * 0.01).normalized()
	var ground := sdf.radius + sdf.sample_height(dir * sdf.radius)
	var altitude := 40.0
	for climb in [6000.0, -6000.0]:
		var t := 0.0
		while t < 7.0:
			await get_tree().process_frame
			t += get_process_delta_time()
			altitude = maxf(40.0, altitude + climb * get_process_delta_time())
			player.global_position = volume.global_transform * (dir * (ground + altitude))
	var idle := 0
	var start := Time.get_ticks_msec()
	while idle < 30 and Time.get_ticks_msec() - start < 30000:
		player.global_position = volume.global_transform * (dir * (ground + altitude))
		await get_tree().process_frame
		idle = idle + 1 if not volume.is_building() and not volume.is_meshing() else 0
	await get_tree().physics_frame
	var turned := 0
	var shown := 0
	for c in volume.get_children():
		if c is JarVoxelChunk and c.visible:
			shown += 1
			if not c.transform.basis.is_equal_approx(Basis()) or c.is_set_as_top_level():
				turned += 1
	_check("space round trip: chunks in the volume", shown > 100 and turned == 0, "%d of %d shown chunks off" % [turned, shown])
	var up: Vector3 = volume.global_transform.basis * dir
	var query := PhysicsRayQueryParameters3D.create(player.global_position + up * 10.0, player.global_position - up * 200.0)
	query.exclude = [player.get_rid()]
	var hit := volume.get_world_3d().direct_space_state.intersect_ray(query)
	_check("space round trip: ground solid again", not hit.is_empty())
	scene.queue_free()
	await get_tree().process_frame
	Engine.max_fps = 0
