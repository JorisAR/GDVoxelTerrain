extends SceneTree
## Generates the demo's low-poly feature meshes (vertex colored, base at the
## origin). Run from the demo folder:
##   godot --headless --path . -s res://demo/assets/population/features/make_meshes.gd

const DIR := "res://demo/assets/population/features/"

func _init() -> void:
	ResourceSaver.save(_pine(), DIR + "pine_mesh.tres")
	ResourceSaver.save(_rock(), DIR + "rock_mesh.tres")
	quit()

func _pine() -> ArrayMesh:
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	_cone(st, 0.0, 2.0, 0.25, 0.2, 6, Color(0.36, 0.24, 0.14))
	_cone(st, 1.2, 5.0, 1.9, 0.0, 8, Color(0.13, 0.30, 0.16))
	_cone(st, 3.2, 6.5, 1.4, 0.0, 8, Color(0.15, 0.34, 0.18))
	_cone(st, 5.0, 8.0, 0.9, 0.0, 8, Color(0.17, 0.38, 0.20))
	st.generate_normals()
	return st.commit()

## A cone (or frustum) from height y0 to y1, with a cap at the bottom.
func _cone(st: SurfaceTool, y0: float, y1: float, r0: float, r1: float, sides: int, color: Color) -> void:
	st.set_color(color)
	for i in sides:
		var a0 := TAU * i / sides
		var a1 := TAU * (i + 1) / sides
		var b0 := Vector3(cos(a0) * r0, y0, sin(a0) * r0)
		var b1 := Vector3(cos(a1) * r0, y0, sin(a1) * r0)
		var t0 := Vector3(cos(a0) * r1, y1, sin(a0) * r1)
		var t1 := Vector3(cos(a1) * r1, y1, sin(a1) * r1)
		# Godot front faces are clockwise seen from outside.
		st.add_vertex(b0); st.add_vertex(b1); st.add_vertex(t1)
		if r1 > 0.0:
			st.add_vertex(b0); st.add_vertex(t1); st.add_vertex(t0)
		st.add_vertex(Vector3(0, y0, 0)); st.add_vertex(b1); st.add_vertex(b0)

func _rock() -> ArrayMesh:
	var sphere := SphereMesh.new()
	sphere.radial_segments = 9
	sphere.rings = 5
	var arrays := sphere.get_mesh_arrays()
	var verts: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var noise := FastNoiseLite.new()
	noise.frequency = 1.3
	var st := SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	st.set_color(Color(0.45, 0.44, 0.42))
	var indices: PackedInt32Array = arrays[Mesh.ARRAY_INDEX]
	for i in indices:
		var v := verts[i]
		# Lumpy, flattened, with its widest part a little below the ground.
		var bumped := v * (1.0 + 0.35 * noise.get_noise_3dv(v)) * Vector3(1.2, 0.7, 1.0)
		st.add_vertex(bumped + Vector3(0, 0.15, 0))
	st.generate_normals()
	return st.commit()
