@tool
extends Label
## What the population's distance settings give each feature, kept up to date.

var population: JarVoxelPopulation
var _timer := 0.0

func _ready() -> void:
	theme_type_variation = &"EditorHelpBitContent"
	refresh()

func _process(delta: float) -> void:
	_timer += delta
	if _timer >= 0.5 and is_visible_in_tree():
		_timer = 0.0
		refresh()

func refresh() -> void:
	var lines := PackedStringArray()
	for row: Dictionary in population.get_feature_distances():
		var impostor: float = row.impostor
		lines.append("%s: %s%s" % [row.name, ("impostor %d m, " % impostor) if impostor > 0.0 else "",
			("gone %.1f km" % (row.draw / 1000.0)) if row.draw >= 1000.0 else ("gone %d m" % row.draw)])
	for row: Dictionary in population.get_detail_distances():
		var far: float = row.far
		lines.append("%s: %sgone %d m" % [row.name, ("%d%% from %d m, " % [row.density * 100.0, far]) if far > 0.0 else "", row.draw])
	text = "\n".join(lines)
