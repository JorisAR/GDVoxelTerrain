@tool
extends Control
## A JarClimateBiomeMap as a picture: biomes are dots in (temperature, moisture)
## space, drag them around. Colors show which biome a climate gets, blended as
## the map blends them.

const SIZE := 260
const SHADES := 72 # pixels per side of the background
const DOT := 7.0
const COLORS: Array[Color] = [Color("5fa55a"), Color("c9a24b"), Color("9fb7c9"), Color("b5655a"), Color("6d7fcc"),
	Color("a66cb5"), Color("4fa3a5"), Color("c47f3f")]

## With one, drags can be undone; without, they just apply.
var undo: EditorUndoRedoManager
var _map: JarClimateBiomeMap
var _texture: ImageTexture
var _dragging := -1
var _start := Vector2.ZERO

func _init() -> void:
	custom_minimum_size = Vector2(SIZE, SIZE + 24)

func set_map(map: JarClimateBiomeMap) -> void:
	_map = map
	_refresh()

## Climate (temperature, moisture), both -1..1, to a point of the plot, and back.
func to_point(climate: Vector2) -> Vector2:
	return Vector2(climate.x * 0.5 + 0.5, 0.5 - climate.y * 0.5) * _side()

func to_climate(point: Vector2) -> Vector2:
	var p := point / _side()
	return Vector2(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0).clampf(-1.0, 1.0)

func _side() -> float:
	return minf(size.x if size.x > 0.0 else float(SIZE), float(SIZE))

func _biomes() -> Array:
	return _map.biomes.filter(func(b): return b != null) if _map != null else []

func _climate_of(biome: JarBiome) -> Vector2:
	return Vector2(biome.temperature, biome.moisture)

## The dot under `point`, -1 for none.
func biome_at(point: Vector2) -> int:
	var biomes := _biomes()
	var best := -1
	var nearest := DOT * 2.0
	for i in biomes.size():
		var d := to_point(_climate_of(biomes[i])).distance_to(point)
		if d < nearest:
			nearest = d
			best = i
	return best

## Moves a biome to where `point` is, live (no undo step).
func move_biome(index: int, point: Vector2) -> void:
	var biome: JarBiome = _biomes()[index]
	var climate := to_climate(point)
	biome.temperature = snappedf(climate.x, 0.01)
	biome.moisture = snappedf(climate.y, 0.01)
	_refresh()

func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			_dragging = biome_at(event.position)
			if _dragging >= 0:
				_start = _climate_of(_biomes()[_dragging])
				accept_event()
		elif _dragging >= 0:
			_finish_drag()
	elif event is InputEventMouseMotion and _dragging >= 0:
		move_biome(_dragging, event.position)
		accept_event()

func _finish_drag() -> void:
	var biome: JarBiome = _biomes()[_dragging]
	var end := _climate_of(biome)
	_dragging = -1
	if end == _start:
		return
	if undo == null:
		_map.emit_changed()
		return
	undo.create_action("Move biome")
	for step: Array in [[true, end], [false, _start]]:
		var add_property := undo.add_do_property if step[0] else undo.add_undo_property
		var add_method := undo.add_do_method if step[0] else undo.add_undo_method
		add_property.call(biome, "temperature", step[1].x)
		add_property.call(biome, "moisture", step[1].y)
		add_method.call(_map, "emit_changed")
		add_method.call(self, "_refresh")
	undo.commit_action()

func _refresh() -> void:
	var biomes := _biomes()
	if biomes.is_empty():
		queue_redraw()
		return
	var image := Image.create(SHADES, SHADES, false, Image.FORMAT_RGB8)
	var blend: float = maxf(_map.blend, 0.01)
	for y in SHADES:
		for x in SHADES:
			var climate := Vector2((x + 0.5) / SHADES * 2.0 - 1.0, 1.0 - (y + 0.5) / SHADES * 2.0)
			var color := Color(0, 0, 0)
			var total := 0.0
			for i in biomes.size():
				var d := climate - _climate_of(biomes[i])
				var w := exp(-d.dot(d) / (blend * blend))
				color += COLORS[i % COLORS.size()] * w
				total += w
			image.set_pixel(x, y, Color(color.r / total, color.g / total, color.b / total))
	if _texture == null:
		_texture = ImageTexture.create_from_image(image)
	else:
		_texture.update(image)
	queue_redraw()

func _draw() -> void:
	var side := _side()
	if _texture != null:
		draw_texture_rect(_texture, Rect2(0, 0, side, side), false)
	var font := get_theme_default_font()
	var text_size := get_theme_default_font_size()
	var biomes := _biomes()
	for i in biomes.size():
		var at := to_point(_climate_of(biomes[i]))
		draw_circle(at, DOT + 1.5, Color.BLACK)
		draw_circle(at, DOT, COLORS[i % COLORS.size()].lightened(0.3))
		var label: String = biomes[i].resource_name if biomes[i].resource_name != "" else "Biome %d" % i
		var width := font.get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, text_size).x
		var place := Vector2(clampf(at.x + DOT + 4.0, 0.0, side - width), at.y + text_size * 0.35)
		draw_string_outline(font, place, label, HORIZONTAL_ALIGNMENT_LEFT, -1, text_size, 3, Color.BLACK)
		draw_string(font, place, label, HORIZONTAL_ALIGNMENT_LEFT, -1, text_size, Color.WHITE)
	var bottom := side + text_size + 4.0
	draw_string(font, Vector2(0, bottom), "Cold", HORIZONTAL_ALIGNMENT_LEFT, -1, text_size)
	draw_string(font, Vector2(0, bottom), "Hot", HORIZONTAL_ALIGNMENT_RIGHT, side, text_size)
	draw_string(font, Vector2(0, bottom), "Dry to wet: bottom to top", HORIZONTAL_ALIGNMENT_CENTER, side, text_size,
		Color(1, 1, 1, 0.6))
