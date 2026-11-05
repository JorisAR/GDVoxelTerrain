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

## The volume's population node, added if it has none.
static func population(volume: JarVoxelVolume) -> JarVoxelPopulation:
	var found := volume.find_children("*", "JarVoxelPopulation", false, false)
	if not found.is_empty():
		return found[0]
	var node := JarVoxelPopulation.new()
	node.table = JarPopulationTable.new()
	volume.add_child(node)
	return node

## Makes `assets` (features or details) grow everywhere, replacing those of that kind.
static func populate(volume: JarVoxelVolume, assets: Array) -> void:
	var node := population(volume)
	var table := JarPopulationTable.new()
	for row: Dictionary in node.table.rows:
		if row.asset.get_class() != assets[0].get_class():
			table.add(row.asset, row.biomes)
	for asset: Resource in assets:
		table.add(asset)
	node.table = table
