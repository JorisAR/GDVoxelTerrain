extends Node
## Headless checks for the SDF library. Run:
##   godot --headless --path demo res://tests/sdf_tests.tscn
## Prints PASS/FAIL per check and exits with code 1 if anything failed.

var _failures := 0

func _ready() -> void:
	_test_primitives()
	_test_modifiers()
	_test_mesh_sdfs()
	_test_eroded_terrain()
	_test_eroded_planet()
	_test_craters()
	print("SDF tests: %s" % ("ALL PASSED" if _failures == 0 else "%d FAILED" % _failures))
	get_tree().quit(1 if _failures > 0 else 0)

func _check(name: String, ok: bool, detail := "") -> void:
	if not ok:
		_failures += 1
	print("%s %s %s" % ["PASS" if ok else "FAIL", name, detail])

func _near(a: float, b: float, eps := 1e-3) -> bool:
	return absf(a - b) <= eps

## Every primitive: inside at its center, outside beyond its bounds, and
## bounds that actually contain the surface.
func _test_primitives() -> void:
	var sphere := JarSphereSdf.new()
	sphere.radius = 2.0
	_check("sphere distance", _near(sphere.sample(Vector3(5, 0, 0)), 3.0))
	var box := JarBoxSdf.new()
	box.extent = Vector3(1, 2, 3)
	box.rounding = 0.5
	_check("rounded box inside", box.sample(Vector3.ZERO) < 0.0)
	_check("rounded box face distance", _near(box.sample(Vector3(3, 0, 0)), 2.0))
	var cone := JarConeSdf.new()
	cone.height = 4.0
	cone.bottom_radius = 2.0
	cone.top_radius = 0.0
	_check("cone inside near base", cone.sample(Vector3(0, -1.5, 0)) < 0.0)
	_check("cone outside near tip", cone.sample(Vector3(1.5, 1.8, 0)) > 0.0)
	var ell := JarEllipsoidSdf.new()
	ell.radii = Vector3(3, 1, 2)
	_check("ellipsoid surface x", _near(ell.sample(Vector3(3, 0, 0)), 0.0, 1e-2))
	_check("ellipsoid outside y", ell.sample(Vector3(0, 1.5, 0)) > 0.0)

	for sdf: JarSignedDistanceField in [sphere, box, cone, ell, JarCapsuleSdf.new(), JarCylinderSdf.new(), JarTorusSdf.new()]:
		var b := sdf.get_bounds()
		var ok := sdf.is_bounded()
		# Corners of a slightly grown box must be outside the solid.
		var grown := b.grow(0.01)
		for i in 8:
			ok = ok and sdf.sample(grown.get_endpoint(i)) > 0.0
		_check("bounds %s" % sdf.get_class(), ok)
	_check("plane unbounded", not JarPlaneSdf.new().is_bounded())

func _test_modifiers() -> void:
	var sphere := JarSphereSdf.new()
	sphere.radius = 1.0
	var offset := JarOffsetSdf.new()
	offset.sdf = sphere
	offset.offset = 0.5
	_check("offset grows", _near(offset.sample(Vector3(1.5, 0, 0)), 0.0))
	offset.shell_thickness = 0.2
	_check("shell hollow", offset.sample(Vector3.ZERO) > 0.0)

	var rep := JarRepeatSdf.new()
	rep.sdf = sphere
	rep.spacing = Vector3(4, 4, 4)
	rep.count = Vector3i(3, 1, 1)
	_check("repeat copy", rep.sample(Vector3(4, 0, 0)) < 0.0)
	_check("repeat limited", rep.sample(Vector3(8, 0, 0)) > 0.0)
	_check("repeat bounds", rep.get_bounds().size.x > 9.9)

	var t := JarTransformSdf.new()
	var box := JarBoxSdf.new()
	box.extent = Vector3(2, 0.5, 0.5)
	t.sdf = box
	t.transform = Transform3D(Basis(Vector3.FORWARD, PI / 2), Vector3(10, 0, 0))
	_check("transform rotates", t.sample(Vector3(10, 1.5, 0)) < 0.0 and t.sample(Vector3(11.5, 0, 0)) > 0.0)
	_check("transform bounds", t.get_bounds().has_point(Vector3(10, 1.9, 0)))

	var op := JarOperationSdf.new()
	op.sdf_a = box
	op.sdf_b = sphere
	op.operation = JarVoxelVolume.SDF_OPERATION_SUBTRACTION
	_check("subtraction carves", op.sample(Vector3.ZERO) > 0.0 and op.sample(Vector3(1.8, 0, 0)) < 0.0)

## Baked meshes must agree with the analytic shape to within about a cell.
func _test_mesh_sdfs() -> void:
	var box_mesh := BoxMesh.new()
	box_mesh.size = Vector3(4, 2, 6)
	var box := JarBoxSdf.new()
	box.extent = Vector3(2, 1, 3)
	_compare_mesh("box mesh", box_mesh, box)

	var sphere_mesh := SphereMesh.new()
	sphere_mesh.radius = 2.0
	sphere_mesh.height = 4.0
	sphere_mesh.radial_segments = 64
	sphere_mesh.rings = 32
	var sphere := JarSphereSdf.new()
	sphere.radius = 2.0
	_compare_mesh("sphere mesh", sphere_mesh, sphere)

	var torus_mesh := TorusMesh.new()
	torus_mesh.inner_radius = 1.5
	torus_mesh.outer_radius = 2.5
	torus_mesh.rings = 64
	torus_mesh.ring_segments = 32
	var torus := JarTorusSdf.new()
	torus.major_radius = 2.0
	torus.minor_radius = 0.5
	_compare_mesh("torus mesh (hole)", torus_mesh, torus)

func _compare_mesh(name: String, mesh: Mesh, reference: JarSignedDistanceField) -> void:
	var sdf := JarMeshSdf.new()
	sdf.mesh = mesh
	sdf.resolution = 48
	var t0 := Time.get_ticks_usec()
	sdf.bake()
	var bake_ms := (Time.get_ticks_usec() - t0) / 1000.0
	var cell := (reference.get_bounds().size[reference.get_bounds().get_longest_axis_index()]) / 48.0
	var rng := RandomNumberGenerator.new()
	rng.seed = 1234
	# Inside the baked grid (mesh bounds + padding) the field must match the
	# analytic shape; outside it is only promised to be a lower bound.
	var grid := sdf.get_bounds().grow(sdf.padding * cell * 0.99)
	var region := reference.get_bounds().grow(1.0)
	var max_err := 0.0
	var sign_errors := 0
	var bound_errors := 0
	for i in 4000:
		var p := region.position + Vector3(rng.randf(), rng.randf(), rng.randf()) * region.size
		var expected := reference.sample(p)
		var got := sdf.sample(p)
		if grid.has_point(p):
			max_err = maxf(max_err, absf(expected - got))
		elif got > expected + cell * 0.5: # border carries the interpolation error
			bound_errors += 1
		# Sign can only legitimately differ right at the surface.
		if signf(expected) != signf(got) and absf(expected) > cell:
			sign_errors += 1
	_check(name, sign_errors == 0 and bound_errors == 0 and max_err < cell * 1.5,
		"(max error %.3f, cell %.3f, sign errors %d, bound errors %d, bake %.1f ms)" % [max_err, cell, sign_errors, bound_errors, bake_ms])

## Erosion SDF: surface near the eroded height, sign correct, lower bound far
## away, and continuous (no jumps between nearby points).
func _test_eroded_terrain() -> void:
	var noise := FastNoiseLite.new()
	noise.frequency = 0.003
	for shape in [JarTerrainSdf.SHAPE_FLAT, JarTerrainSdf.SHAPE_SPHERE]:
		var sdf := JarTerrainSdf.new()
		sdf.noise = noise
		sdf.height = 100.0
		sdf.shape = shape
		sdf.radius = 1000.0
		var label: String = ["flat", "sphere"][shape]
		var up := Vector3.UP
		var base := Vector3(37, 0, -12) if shape == JarTerrainSdf.SHAPE_FLAT else Vector3(0.3, 1.0, 0.2).normalized() * 1000.0
		if shape == JarTerrainSdf.SHAPE_SPHERE:
			up = base.normalized()
		var h := sdf.sample_height(base)
		var surface := base + up * h
		_check("eroded %s surface" % label, absf(sdf.sample(surface)) < 0.5, "d=%.3f at height %.1f" % [sdf.sample(surface), h])
		_check("eroded %s sign" % label, sdf.sample(surface + up * 5.0) > 0.0 and sdf.sample(surface - up * 5.0) < 0.0)
		_check("eroded %s far lower bound" % label, sdf.sample(surface + up * 2000.0) <= 2000.0 + 1e-3)
		# The far-field bound is blended in: no step anywhere along the way up.
		var worst_step := 0.0
		for k in 4000:
			var q := surface + up * (k * 0.1)
			worst_step = maxf(worst_step, absf(sdf.sample(q + up * 0.1) - sdf.sample(q)))
		_check("eroded %s continuous upwards" % label, worst_step < 0.15, "largest change over 10 cm: %.3f" % worst_step)
		# Continuous: a 1 mm step never jumps (steep slopes are fine, steps are not).
		# Close to a distance: the octree's subdivision margin tolerates a
		# gradient of about 2.9, so most of the surface must stay well below.
		var max_jump := 0.0
		var slopes: Array[float] = []
		for i in 200:
			var p := surface + Vector3(i * 0.37, 0, i * 0.21)
			max_jump = maxf(max_jump, absf(sdf.sample(p + Vector3(0.001, 0, 0)) - sdf.sample(p)))
			var b := p - up * sdf.sample(p)
			var e := 0.01
			slopes.append(Vector3(sdf.sample(b + Vector3(e, 0, 0)) - sdf.sample(b - Vector3(e, 0, 0)),
				sdf.sample(b + Vector3(0, e, 0)) - sdf.sample(b - Vector3(0, e, 0)),
				sdf.sample(b + Vector3(0, 0, e)) - sdf.sample(b - Vector3(0, 0, e))).length() / (2.0 * e))
		slopes.sort()
		_check("eroded %s continuous" % label, max_jump < 0.02, "largest jump over 1 mm: %.4f" % max_jump)
		_check("eroded %s distance-like" % label, slopes[100] < 1.6 and slopes[190] < 2.9,
			"gradient length median %.2f, 95%% %.2f" % [slopes[100], slopes[190]])

## The demo planet's eroded terrain: no step where the tangent frame of the
## slope estimate used to switch axes (|up.y| = 0.9, a ~2 m cliff along two
## circles of latitude), and the gradient override agrees with differences of
## the distance itself.
func _test_eroded_planet() -> void:
	var sdf: JarTerrainSdf = load("res://demo/planet/planet_terrain.tres")
	var worst := {0.9: 0.0, 0.8: 0.0}
	for y0: float in worst:
		for i in 200:
			var a := TAU * i / 200.0
			var hs: Array[float] = []
			for k in [-3, -1, 1, 3]:
				var y: float = y0 + k * 1e-5
				var r := sqrt(1.0 - y * y)
				hs.append(sdf.sample_height(Vector3(cos(a) * r, y, sin(a) * r) * sdf.radius))
			# Middle difference minus the mean of the outer ones: a step stands out.
			worst[y0] = maxf(worst[y0], absf((hs[2] - hs[1]) - 0.5 * ((hs[1] - hs[0]) + (hs[3] - hs[2]))))
	_check("eroded planet no step at the frame switch", worst[0.9] < 0.3, "%.3f m (elsewhere %.3f m)" % [worst[0.9], worst[0.8]])
	var rng := RandomNumberGenerator.new()
	rng.seed = 5
	var angles: Array[float] = []
	for i in 300:
		var up := Vector3(rng.randf_range(-1, 1), rng.randf_range(-1, 1), rng.randf_range(-1, 1)).normalized()
		var p := up * (sdf.radius + sdf.sample_height(up * sdf.radius) + rng.randf_range(-2.0, 2.0))
		var e := 0.02
		var numeric := Vector3(sdf.sample(p + Vector3(e, 0, 0)) - sdf.sample(p - Vector3(e, 0, 0)),
			sdf.sample(p + Vector3(0, e, 0)) - sdf.sample(p - Vector3(0, e, 0)),
			sdf.sample(p + Vector3(0, 0, e)) - sdf.sample(p - Vector3(0, 0, e)))
		angles.append(rad_to_deg(sdf.sample_normal(p).angle_to(numeric)))
	angles.sort()
	_check("eroded planet gradient matches differences", angles[150] < 1.0 and angles[285] < 3.0,
		"angle median %.2f, 95%% %.2f degrees" % [angles[150], angles[285]])

## Craters on flat ground: bowls lower it, rims raise it a little, the change
## stays within the crater size, and the field stays continuous.
func _test_craters() -> void:
	var ground := JarTerrainSdf.new() # no noise: a flat plane at y = 0
	ground.erosion = false
	var craters := JarCraterSdf.new()
	craters.sdf = ground
	craters.size = 20.0
	craters.density = 0.8
	var lowest := 0.0
	var highest := 0.0
	var max_jump := 0.0
	for i in 4000:
		var p := Vector3(i * 0.37 - 700.0, 0.0, sin(i * 0.1) * 60.0)
		# Height of the surface: the SDF at y = 0 is minus the height change.
		var h := -craters.sample(p)
		lowest = minf(lowest, h)
		highest = maxf(highest, h)
		max_jump = maxf(max_jump, absf(craters.sample(p + Vector3(0.001, 0, 0)) - craters.sample(p)))
	_check("craters dig bowls", lowest < -2.0, "deepest %.2f m" % lowest)
	_check("craters raise rims", highest > 0.2 and highest < lowest * -1.0, "highest rim %.2f m" % highest)
	_check("craters bounded", lowest > -craters.size * (craters.depth + craters.rim) * 3.0, "deepest %.2f m" % lowest)
	_check("craters continuous", max_jump < 0.01, "largest jump over 1 mm: %.4f" % max_jump)
	# A crater field over a planet is still a planet: volumes in auto up mode see it.
	var planet := JarTerrainSdf.new()
	planet.shape = JarTerrainSdf.SHAPE_SPHERE
	craters.sdf = planet
	var volume := JarVoxelVolume.new()
	volume.sdf = craters
	_check("craters keep the child's up field", volume.get_up_field_info().mode == JarVoxelVolume.UP_MODE_RADIAL)
	volume.free()

