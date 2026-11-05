@tool
extends RefCounted
## Makes a population layer from a file: a feature or detail resource as it is;
## a scene or mesh gets a new resource next to it, with a density and footprint
## guessed from its size, which you can then tune. Small meshes become details
## (grass, pebbles), everything else features.

const FILTERS: PackedStringArray = ["*.tscn,*.scn,*.glb,*.gltf,*.blend,*.fbx ; Scenes",
	"*.obj,*.mesh ; Meshes", "*.tres,*.res ; Features and details"]
const DETAIL_SIZE := 1.5 # meters: meshes smaller than this are details

## Size of everything `node` draws, in its own space.
static func _bounds(node: Node, transform: Transform3D, bounds: Array) -> void:
	var here := transform
	if node is Node3D:
		here = transform * node.transform
	if node is MeshInstance3D and node.mesh != null:
		var box: AABB = here * node.mesh.get_aabb()
		bounds[0] = box if bounds[0] == null else bounds[0].merge(box)
	for child in node.get_children():
		_bounds(child, here, bounds)

static func size_of(asset: Resource) -> Vector3:
	if asset is Mesh:
		return asset.get_aabb().size
	var scene: Node = asset.instantiate()
	var bounds: Array = [null]
	_bounds(scene, Transform3D(), bounds)
	scene.free()
	return bounds[0].size if bounds[0] != null else Vector3.ONE

## A resource for `asset` beside it, or the one made before.
static func _resource_for(asset: Resource, path: String) -> Resource:
	var target := path.get_basename() + ("_detail.tres" if _is_detail(asset) else "_feature.tres")
	if ResourceLoader.exists(target):
		return load(target)
	var size := size_of(asset)
	var across := maxf(size.x, size.z)
	var made: Resource
	if _is_detail(asset):
		made = JarTerrainDetail.new()
		made.mesh = asset
		made.density = clampf(0.5 / maxf(across * across, 0.01), 0.1, 8.0)
	else:
		made = JarTerrainFeature.new()
		if asset is PackedScene:
			made.scene = asset
		else:
			made.mesh = asset
		made.footprint = across * 0.5
		# Well spaced, like a thin wood: one per ten footprints of ground.
		made.density = 0.1 / maxf(across * across, 1.0)
	ResourceSaver.save(made, target)
	return load(target)

static func _is_detail(asset: Resource) -> bool:
	return asset is Mesh and size_of(asset).length() < DETAIL_SIZE

## A new layer named after the file, not in the tree yet; null for other files.
static func layer_for(path: String) -> JarPopulationLayer:
	var asset: Resource = load(path)
	if asset is PackedScene or asset is Mesh:
		asset = _resource_for(asset, path)
	var layer: JarPopulationLayer
	if asset is JarTerrainFeature:
		layer = JarFeatureLayer.new()
		layer.feature = asset
	elif asset is JarTerrainDetail:
		layer = JarDetailLayer.new()
		layer.detail = asset
	else:
		return null
	layer.name = path.get_file().get_basename().to_pascal_case()
	return layer
