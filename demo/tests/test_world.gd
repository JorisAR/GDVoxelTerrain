class_name TestWorld
## Builds minimal, demo-independent worlds for the headless tests.

const CHUNK_SCENE := preload("res://addons/jar_voxel_terrain/src/chunk.tscn")
const TERRAIN_SDF := preload("res://demo/terrain_sdf.tres")

## A flat noise terrain volume plus a viewer, added under `parent`.
## Returns {volume, viewer}.
static func flat(parent: Node, viewer_position: Vector3, configure := Callable()) -> Dictionary:
	var volume := JarVoxelVolume.new()
	volume.chunk_scene = CHUNK_SCENE
	volume.sdf = TERRAIN_SDF
	if configure.is_valid():
		configure.call(volume)
	parent.add_child(volume)
	var viewer := JarVoxelViewer.new()
	viewer.position = viewer_position
	parent.add_child(viewer)
	return {volume = volume, viewer = viewer}
