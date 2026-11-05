@tool
extends VBoxContainer
## Bottom panel: a JarPopulationTable as a table, assets by biome. A cell is a
## density multiplier, empty where the asset does not grow. Drop scenes, meshes
## or feature / detail resources on it to add rows. Everything is written to
## the table at once, so its .tres stays the plain text it always was.

const ALL := "*"
const FIRST_BIOME_COLUMN := 2 # asset, group, then "All" and the biomes

var population: JarVoxelPopulation
var _tree: Tree
var _search: LineEdit
var _only := "" # show only rows growing in this biome
var _columns: Array[String] = []

func _init() -> void:
	name = "Population"
	custom_minimum_size.y = 220
	var bar := HBoxContainer.new()
	add_child(bar)
	_search = LineEdit.new()
	_search.placeholder_text = "Filter by name or group"
	_search.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_search.text_changed.connect(func(_text: String): refresh())
	bar.add_child(_search)
	for action: Array in [["Move up", move_selected.bind(-1)], ["Move down", move_selected.bind(1)],
			["Remove", remove_selected]]:
		var button := Button.new()
		button.text = action[0]
		button.pressed.connect(action[1])
		bar.add_child(button)
	_tree = Tree.new()
	_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_tree.hide_root = true
	_tree.column_titles_visible = true
	_tree.select_mode = Tree.SELECT_ROW
	_tree.item_edited.connect(_on_edited)
	_tree.item_selected.connect(_on_selected)
	_tree.column_title_clicked.connect(_on_title_clicked)
	_tree.set_drag_forwarding(Callable(), _can_drop, _drop)
	add_child(_tree)

func set_population(value: JarVoxelPopulation) -> void:
	population = value
	_only = ""
	refresh()

func _table() -> JarPopulationTable:
	if population == null:
		return null
	if population.table == null:
		population.table = JarPopulationTable.new()
	return population.table

func _biomes() -> Array[String]:
	var result: Array[String] = []
	var volume := population.get_parent() as JarVoxelVolume if population != null else null
	if volume != null and volume.population_biome_map != null:
		for biome: JarBiome in volume.population_biome_map.biomes:
			if biome != null:
				result.append(biome.resource_name)
	return result

func _store(rows: Array[Dictionary]) -> void:
	var table := _table()
	table.rows = rows
	if table.resource_path != "" and not table.resource_path.contains("::"):
		ResourceSaver.save(table)
	refresh()

func refresh() -> void:
	_tree.clear()
	var table := _table()
	if table == null:
		return
	_columns.assign([ALL] + _biomes())
	# Names in the table that no biome has any more stay visible, to fix them.
	for row: Dictionary in table.rows:
		for name: String in row.get("biomes", {}):
			if not name in _columns:
				_columns.append(name)
	_tree.columns = FIRST_BIOME_COLUMN + _columns.size()
	_tree.set_column_title(0, "Asset")
	_tree.set_column_title(1, "Group")
	_tree.set_column_expand(0, true)
	_tree.set_column_custom_minimum_width(0, 180)
	for i in _columns.size():
		var title := "All" if _columns[i] == ALL else _columns[i]
		_tree.set_column_title(FIRST_BIOME_COLUMN + i, ("> " + title) if _columns[i] == _only else title)
	var root := _tree.create_item()
	var groups := {}
	var filter := _search.text.to_lower()
	for r in table.rows.size():
		var row: Dictionary = table.rows[r]
		var group: String = row.get("group", "")
		var title := _asset_name(row)
		if filter != "" and not filter in title.to_lower() and not filter in group.to_lower():
			continue
		if _only != "" and not row.get("biomes", {}).has(_only):
			continue
		if not groups.has(group):
			groups[group] = _tree.create_item(root)
			groups[group].set_text(0, group if group != "" else "(no group)")
			groups[group].set_selectable(0, false)
		var item := _tree.create_item(groups[group])
		item.set_metadata(0, r)
		item.set_text(0, title)
		item.set_text(1, group)
		item.set_editable(1, true)
		var biomes: Dictionary = row.get("biomes", {})
		for i in _columns.size():
			var column := FIRST_BIOME_COLUMN + i
			item.set_editable(column, true)
			item.set_text(column, str(biomes[_columns[i]]) if biomes.has(_columns[i]) else "")

func _asset_name(row: Dictionary) -> String:
	var asset: Resource = row.get("asset")
	if asset == null:
		return "(missing)"
	if asset.resource_name != "":
		return asset.resource_name
	return asset.resource_path.get_file().get_basename()

## `text`: a density multiplier; empty removes the biome from the row.
func set_cell(row: int, biome: String, text: String) -> void:
	var rows := _table().rows
	var biomes: Dictionary = rows[row].get("biomes", {}).duplicate()
	if text.strip_edges() == "" or not text.is_valid_float():
		biomes.erase(biome)
	else:
		biomes[biome] = text.to_float()
	rows[row]["biomes"] = biomes
	_store(rows)

func set_group(row: int, group: String) -> void:
	var rows := _table().rows
	rows[row]["group"] = group
	_store(rows)

func move_row(row: int, by: int) -> void:
	var rows := _table().rows
	var to := row + by
	if to < 0 or to >= rows.size():
		return
	var moved := rows[row]
	rows[row] = rows[to]
	rows[to] = moved
	_store(rows)

func move_selected(by: int) -> void:
	var item := _tree.get_selected()
	if item != null and item.get_metadata(0) != null:
		move_row(item.get_metadata(0), by)

func remove_selected() -> void:
	var item := _tree.get_selected()
	if item == null or item.get_metadata(0) == null:
		return
	var rows := _table().rows
	rows.remove_at(item.get_metadata(0))
	_store(rows)

func _on_edited() -> void:
	var item := _tree.get_edited()
	var column := _tree.get_edited_column()
	var row: int = item.get_metadata(0)
	if column == 1:
		set_group.call_deferred(row, item.get_text(1))
	else:
		set_cell.call_deferred(row, _columns[column - FIRST_BIOME_COLUMN], item.get_text(column))

func _on_selected() -> void:
	var item := _tree.get_selected()
	if item == null or item.get_metadata(0) == null or not Engine.is_editor_hint():
		return
	var asset: Resource = _table().rows[item.get_metadata(0)].get("asset")
	if asset != null:
		EditorInterface.edit_resource(asset)

func _on_title_clicked(column: int, _button: int) -> void:
	if column < FIRST_BIOME_COLUMN:
		return
	var biome := _columns[column - FIRST_BIOME_COLUMN]
	_only = "" if _only == biome else biome
	refresh()

func _can_drop(_at: Vector2, data: Variant) -> bool:
	return population != null and data is Dictionary and data.get("type") == "files"

func _drop(_at: Vector2, data: Variant) -> void:
	for path: String in data.files:
		add_file(path)

## Adds a row for a feature, detail, scene or mesh file; a scene or mesh gets a
## new feature resource next to the table (or, unsaved, next to the file).
func add_file(path: String) -> void:
	var asset := load(path)
	if asset is PackedScene or asset is Mesh:
		var feature := JarTerrainFeature.new()
		if asset is PackedScene:
			feature.scene = asset
		else:
			feature.mesh = asset
		var folder := _table().resource_path.get_base_dir()
		if folder == "":
			folder = path.get_base_dir()
		var target := folder.path_join(path.get_file().get_basename() + "_feature.tres")
		if ResourceLoader.exists(target):
			feature = load(target)
		else:
			ResourceSaver.save(feature, target)
			feature = load(target)
		asset = feature
	if not (asset is JarTerrainFeature or asset is JarTerrainDetail):
		return
	var rows := _table().rows
	rows.append({"group": "", "asset": asset, "biomes": {ALL: 1.0}})
	_store(rows)
