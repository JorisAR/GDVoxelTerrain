# Godot Voxel Terrain Plugin

![Banner Image](https://github.com/JorisAR/GDVoxelTerrain/blob/main/banner.png?raw=true)

This project adds a smooth voxel terrain system to godot. 
More precisely, it uses an octree to store an SDF, which is then meshed using a custom version of surface nets. Level of detail systems are in place for large viewing distances.

Volumes are fully editable at runtime (dig / add anywhere), can be flat worlds, planets or moons, can move and rotate freely (a moon can orbit a planet), and can be shaped entirely from data using composable SDF resources. Any number of volumes can share one scene.

## Getting Started

### Requirements

- **Godot 4.4.1** or later (the `godot-cpp` submodule tracks the `4.4` branch).
- **Python 3** with **SCons** (`pip install scons`).
- A **C++17** compiler:
  - Linux: GCC (`build-essential`), tested on Linux Mint 22 / Ubuntu 24.04.
  - Windows: MSVC or MinGW, tested on Windows 11.

### Building

```bash
git clone --recursive https://github.com/JorisAR/GDVoxelTerrain
cd GDVoxelTerrain
scons platform=linux  target=template_debug   # or platform=windows / macos
scons platform=linux  target=template_release
```

The compiled library is written straight into `demo/addons/jar_voxel_terrain/bin/`.

### Installation

- Copy `demo/addons/jar_voxel_terrain` into the `addons` folder of your project.
- Prebuilt binaries for tagged releases are attached by the GitHub Actions release workflow.

### Trying the demo

Open the `demo` project in Godot and run one of the scenes in `demo/demo`:

- `planet.tscn` (the main scene): a forest planet of 16 km radius with oceans, eroded mountain belts (warped ridged ranges with peaks and foothills) with snowy peaks and no trees, tundra and steppe biomes, water, an atmosphere, clouds a day and night, and a cratered moon (1.3 km radius, 60 km away) you can fly to and land on (it carries you along). 16 km keeps everything well within 32-bit float precision (about 2 mm at the surface). Its terrain, materials and population live in `demo/demo/planet/`; change the radius in `planet_terrain.tres` (and the volume's `extent`) and the water, atmosphere, materials and population rules follow.
- `flat.tscn`: flat terrain with a crater carved by an SDF operation (`op_carve.tres`), water and the same population.

Controls: `WASD` move, `Space` jump, `Shift` sprint, `Tab` toggle flying (`Space`/`Shift` up/down, `Ctrl` + scroll speed), `M` toggle surface nets / dual contouring, `Esc` release the mouse.
Editing: `left click` dig, `right click` add, `Q` next brush (sphere, box, cone, prism, torus), `scroll` brush size, `E` smooth blending, `1`-`8` material (of the built-in set), `P` paint mode (left click paints instead of digging), `R` toggle the brush preview.

The editing scripts in `demo/demo/editing` are meant to be reused: `SdfEditor` edits with any SDF resource you assign (plus an optional preview mesh), `BrushEditor` adds the built-in brushes. `demo/demo/player` contains the character controller, which follows `JarVoxelUniverse` gravity around celestial bodies (Godot's gravity otherwise) and moves along with the body whose gravity dominates (e.g. an orbiting moon).

## Usage

- Please refer to the demo scenes to see how to use the terrain system.
- The essentials for your own scene:
  1. Add a `JarVoxelVolume` node anywhere in the scene (it can be moved, rotated or animated).
  2. Assign its `sdf` (any `JarSignedDistanceField` resource — this defines the starting shape) and its `chunk_scene` (`addons/jar_voxel_terrain/src/chunk.tscn`).
  3. With a `JarTerrainSdf` the default `up_mode` (*Auto*) takes the up direction and sea level from the SDF: radial for planets, +Y for flat terrain. For other SDFs pick *Axis* or *Radial* and set `sea_level` (the planet radius for radial). This drives the material, population rules, water and `get_up_direction_at()` / `get_height_at()`.
  4. Add a `JarVoxelViewer` as a child of your player or camera. Without one, volumes follow the active camera.
  5. Optional: put the volume under a `JarCelestialBody` for gravity (plain rigid bodies fall toward it with no setup), water, atmosphere and clouds (see [Celestial bodies](#celestial-bodies)).
- Edit volumes at runtime from scripts with `volume.sphere_edit(position, radius, union)` or the more general `volume.modify(...)`. To edit whatever a physics raycast hit: `hit.collider.get_parent().get_volume()`.

### Migrating from 0.x (`JarVoxelTerrain`)

- `JarVoxelTerrain` is now `JarVoxelVolume`.
- `player_node` is replaced by `JarVoxelViewer` nodes (add one under your player/camera).
- `JarWorld`, `JarPlanarWorld` and `JarSphericalWorld` are gone: use a plain `Node3D` and set the volume's `up_mode`, `up_center` and `sea_level` (radius) instead. For gravity, use a `JarCelestialBody` (see [Celestial bodies](#celestial-bodies)).
- `performance_max_concurrent_tasks` is gone: all volumes share one worker pool sized to the CPU.
- Operation constants are now `JarVoxelVolume.SDF_OPERATION_UNION` etc.
- `octree_scale` + `size` are replaced by `voxel_size` (meters per voxel at full detail) and `extent` (meters the volume covers); `size = n` corresponds to `extent = 2^n * voxel_size`. `min_chunk_size` and the unused `lod_level_count` are gone (chunks are always 16 voxels).
- Population rules: `minimum_slope` (a cosine) is replaced by `maximum_slope` in degrees (`minimum_slope = 0.85` becomes `maximum_slope = 32`); the unused `max_lod` is gone. Height ranges now default to unlimited.
- `JarPlanetSdf` and the old `JarTerrainSdf` are merged into `JarTerrainSdf`: set `shape` to *Sphere* and `radius` for planets. The old `noise_scale` / `height_scale` become `height` (above zero) and `depth` (below); the old terrain used `height = 2 * height_scale`, `depth = height_scale`, the old planet `height = noise_scale`, `depth = noise_scale / 2`. Set `erosion = false` for the old look.
- `edit()`, `modify()` and `sphere_edit()` default to `material = -1` (keep existing materials) instead of painting material 0; pass a material to paint.
- `up_mode` defaults to the new *Auto*; volumes that set *Axis* or *Radial* explicitly keep working.
- `moon.tscn` is merged into `planet.tscn`; the old water material and the compositor atmosphere of the demo are replaced by `JarBodyWater` and `JarBodyAtmosphere`.
- The chunk shader `chunk.gdshader` is replaced by the material-mode shaders (see [Materials](#materials)); the chunk scene no longer carries a material (the volume assigns one). Its `world_center` / `world_radius` parameters are gone (the volume's up field is used). Colors per material moved from shader parameters to `JarTerrainMaterial` resources in the volume's `materials`.

### Shaping worlds with SDFs

Shapes are authored as composable `JarSignedDistanceField` resources:

- **Primitives:** `JarSphereSdf`, `JarBoxSdf` (optionally rounded), `JarPlaneSdf`, `JarCylinderSdf`, `JarCapsuleSdf`, `JarTorusSdf`, `JarConeSdf`, `JarEllipsoidSdf`.
- **Terrain:** `JarTerrainSdf`, a flat heightfield or a planet (`shape`), built from base noise (separate `height` and ocean `depth`), optional mountain ranges (a low-frequency mask raising mountains where it passes a threshold, shaped by a second, e.g. ridged, noise) and optional erosion (branching gullies and sharp ridges, evaluated per point, after [runevision's erosion filter](https://blog.runevision.com/2026/03/fast-and-gorgeous-erosion-filter.html)). Its noises are `JarNoise` (the extension's own simplex noise with exact gradients, FBM or ridged fractals and domain warp: about twice as fast to build, with more accurate distances and normals) or `FastNoiseLite`. Only the settings that apply are shown in the inspector. Surface nets suit eroded terrain best; keep the smallest gullies a few voxels wide.
- **Meshes:** `JarMeshSdf` turns any closed triangle mesh into an SDF. It is baked once into a distance grid stored in the resource (press *Bake* in the inspector, or it bakes on first use).
- **Combining:** `JarOperationSdf` (union / subtraction / intersection and smooth variants, nestable for full CSG) and `JarTransformSdf` (position / rotate / scale a child).
- **Modifiers:** `JarOffsetSdf` (grow, shrink, hollow into a shell), `JarRepeatSdf` (grid copies), `JarDisplaceSdf` (noise displacement), `JarCraterSdf` (impact craters on any terrain: bowls with rims in several sizes, measured along the surface; the demo moon uses it).
- Every SDF can be queried from scripts: `sdf.sample(position)`, `sdf.sample_normal(position)`, `sdf.get_bounds()`.

Edit volumes with any bounded SDF, placed with a full transform:

```gdscript
var box := JarBoxSdf.new()
box.extent = Vector3(2, 4, 2)
volume.edit(box, JarVoxelVolume.SDF_OPERATION_UNION, Transform3D(Basis(Vector3.UP, PI / 4), hit_position))
```

In the demos, `Q` cycles the brush (sphere, box, cone, prism mesh) and `E` toggles smooth blending. See `demo/demo/flat.tscn` + `demo/demo/op_carve.tres` for a crater carved out of noise terrain with a single resource.

### Materials

A volume's `materials` is a list of `JarTerrainMaterial` resources (color, roughness, procedural variation, optional albedo / normal textures); the index is the material id. Without any, a built-in set is used: 0 grass, 1 rock, 2 sand, 3 seabed, 4 snow, 5 dirt, 6 dark stone, 7 clay.

Unpainted ground is covered by **ground rules** in the shader: ground (flat), cliff (steeper than `cliff_angle`), shore (up to `shore_height` above sea level), seabed (below sea level) and peaks (above `peak_height`), with noise-broken borders. All heights are relative to the volume's sea level, so they stay right when you change a planet's radius. Edits paint their `material` argument (`-1` keeps what is there), and `volume.paint(sdf, transform, material)` paints inside a shape without changing it:

```gdscript
var brush := JarSphereSdf.new()
brush.radius = 4.0
volume.paint(brush, Transform3D(Basis(), hit_position), 5)
```

Each voxel keeps its 4 strongest materials. `material_mode` picks how they reach the shader, each with a default shader in `addons/jar_voxel_terrain/src/shaders/`:

| Mode | Materials | Borders | Vertex data |
|---|---|---|---|
| **Weights** (default) | 0-7 | smooth blends | 8 weights (`COLOR` + `CUSTOM0`) |
| **Split** | 0-254 | crisp, one material per triangle | id in `CUSTOM0.r` (vertices split at borders) |
| **Indexed** | 0-254 | smooth blends | 4 ids per triangle in `CUSTOM0`, weights in `COLOR` (vertices split where id sets differ, so no seams) |

The default shaders are procedural (no textures needed): large brightness / hue variation, fine detail and bump, fading with distance. Give materials textures and they are applied with triplanar mapping. Everything is computed in the volume's own space, so textures and rules follow moving volumes and work on planets and flat worlds alike.

To customize, set the volume's `material` to your own `ShaderMaterial` (a copy of a default material to tweak its rule and look settings, or your own shader). The volume copies it per volume and fills in the material palette, texture arrays, its transform and up field (chunks need no instance uniforms). The easiest way to write a new shader is to include `terrain_common.gdshaderinc` like the defaults do; the inputs are documented there and on `JarVoxelVolume.material`.

### Population

Volumes populate themselves with two kinds of objects (the approach is described in [Population of a Large-Scale Terrain](https://jorisar.nl/blog/Population/)):

Add a `JarVoxelPopulation` node as a child of the volume and give it a `JarPopulationTable`; without the node nothing of this runs.

- **Details** (`JarTerrainDetail`): grass and other small things, scattered over the triangles of full-detail chunks on the mesh threads and drawn with a `MultiMesh` per chunk. Cheap, and they follow edits automatically.
- **Features** (`JarTerrainFeature`): trees, rocks, structures. They are placed on the SDF surface (Poisson disc samples ray marched down onto it), independent of the mesh resolution, so they are visible from afar (`draw_distance`) and always stand in the same place. Use a `mesh` (instanced, cheapest) or a `scene` (for colliders and scripts). Features are generated on worker threads in cells around the viewers (`feature_cell_size` on the node).

Both share placement rules, all of them probabilities so populations thin out naturally instead of stopping at hard lines:

- **Rules:** a height range above sea level and a maximum slope, each with a falloff.
- **Distribution:** a noise mask with a soft threshold (forests, meadows; `mask_invert` puts grass between the trees of a forest using the same noise) and clusters (groves of `cluster_radius` about `cluster_spacing` apart).
- **Placement:** scale range, random rotation, alignment to the normal, sink.

Features are placed as layers in table order, and each keeps the ones after it out of its `footprint`: list trees first, then rocks, then bushes, and they fill the gaps without overlapping. Edits remove the features standing where the shape changes, permanently; for gameplay there are `remove_features(position, radius)` and `get_features_near(position, radius)`:

```gdscript
# Chop the tree the player is looking at.
for f in population.get_features_near(hit_position, 2.0):
	volume.remove_features(f.transform.origin, 0.5)
```

Features can bake their own impostor: enable the *JAR Voxel Terrain* plugin (Project Settings, Plugins), then press *Bake impostor* on a `JarTerrainFeature`, or *Bake all impostors* on a `JarVoxelPopulation`. Baking needs the Forward+ renderer and takes about 5 seconds per feature. The bake is saved next to the feature and takes over from `impostor_distance` (0: 10 times its size); give a scene as many LODs as you like, its last one reaches the impostor.

The demos use a generated low-poly pine and rock (`demo/demo/assets/population/features/`).

### Biomes

A volume's `population_biome_map` decides which `JarBiome` is where. `JarBiomeMap` itself is uniform (its first biome everywhere). `JarClimateBiomeMap` uses a simple climate model: temperature from a noise, colder with height above sea level and towards a planet's poles, and moisture from a second noise. Each biome sits at a point in (temperature, moisture) space and positions belong to the biomes nearest their climate, blended smoothly, so borders follow the climate rather than a grid.

A biome brings its own **ground materials** for the shader's ground rules (ground, cliff, shore, seabed, peaks; `-1` keeps the shader's). The mesher passes per-vertex weights of the first 4 biomes in `CUSTOM1`.

What grows in each biome is in the `JarPopulationTable`: one row per feature or detail, naming biomes by `resource_name`, each with a density multiplier. `"*"` is every other biome, or everywhere when alone:

```
{ "group": "Trees", "asset": ExtResource("pine"), "biomes": { "Temperate forest": 1.0, "Alpine tundra": 0.3 } }
```

From a script: `table.add(pine, {"Temperate forest": 1.0})`.

`get_biome_weights_at(position)` tells scripts which biomes are where. The demo planet has a temperate forest (most of it), alpine tundra (high ground and poles) and a dry steppe (`demo/demo/planet/biomes.tres`).

### Celestial bodies

A `JarCelestialBody` is a planet, moon or star: the frame (center, up, radius, sea level) and gravity that everything attached to it shares. Put its `JarVoxelVolume` under it (or set `volume`); without a volume (a distant moon) its own `shape`, `radius` and `sea_level` are used. Flat volumes work too: gravity then points down the volume's up axis.

```
Sun (JarCelestialBody)
  Light (JarBodyLight)          drives the scene's DirectionalLight3D
  PlanetOrbit (JarBodyOrbit)
    Planet (JarCelestialBody)
      JarVoxelVolume
      Water (JarBodyWater)
      Atmosphere (JarBodyAtmosphere)
      Clouds (JarBodyClouds)
      MoonOrbit (JarBodyOrbit)
        Moon (JarCelestialBody)
          JarVoxelVolume
```

The scene as laid out in the editor is the universe at its start (orbits show their time 0 position): the first anchor stays exactly where it was placed, and the clock starts at 0 for every new set of bodies (`set_ticks()` sets any other time).

- **Gravity**: `surface_gravity` at the radius, inverse square outside, linear to zero at the center, optionally cut off at `gravity_reach`. The `JarVoxelUniverse` singleton sums all bodies: `JarVoxelUniverse.get_gravity(position)` for strength and `get_up(position)` for up (a sharpened blend, so a nearby moon does not tilt the ground you stand on). Physics runs in the frame of the *anchor* (the body whose sphere of influence the first `JarVoxelViewer` is in, or `set_anchor_override()`): the physics space's default gravity points at it, so plain `RigidBody3D`s and `CharacterBody3D.get_gravity()` work with no `Area3D` at all.
- **Frame switching**: the scene is shown relative to the anchor. Near its surface (below `rotating_frame_height`: the top of its atmosphere, else half its radius) the scene turns with it: the ground stands still (its colliders never move) and everything else, sky and sun included, turns around it. Higher up only its center stands still: the stars stay put and the body turns below. Flying from the planet to the moon makes the moon the anchor once you enter its sphere of influence (well above its surface): it stops where it is, nothing jumps, and free physics bodies keep their motion (velocities converted into the moon's frame; put controllers that handle this themselves in the `jar_frame_exempt` group). Poses are computed from time in doubles every tick, so switching back and forth never drifts. `universe/frame_switching` turns it off.
- **Stars**: a `JarBodyLight` under a body makes it a sun: it aims a `DirectionalLight3D` from the body at the camera every frame, with the body's real angular size for the sky's sun disk (and optionally the light's, for soft shadows). Give the planet a `spin_period` and an orbit around the sun, and day, night and seasons follow from the geometry. The demo's sun is 4000 km away with an 18.6 km radius (0.53 degrees), a 30 minute day and a 10 hour year. The star itself is not drawn; the sky draws its disk. A second `JarBodyLight` on a moon with `reflects` set to the sun's makes moonlight: a dim light from the moon that follows its phase and lights the night side and the clouds. A `JarLensFlare` anywhere in the scene adds a lens flare for the sun (glow, starburst, ghosts), hidden by terrain and moons that cover the sun.
- **Time**: orbits and spin are functions of universe time (integer physics ticks times `time_scale`), so they never drift and are identical on every machine. `JarBodyOrbit` carries its children on a circle around its parent body (`radius`, `period`, `axis`, `phase`); `spin_period` and `spin_axis` turn a body.
- **Floating origin**: once the viewer is `floating_origin/distance` (2048 m) from the scene's origin, the whole scene moves back near it, so 32-bit floats keep their precision near the camera on planets of any size; small worlds never shift. On a body, the shift also aligns that body exactly with the scene axes on a fine grid, which keeps chunk seams watertight. Scripts that keep scene positions of their own connect to `JarVoxelUniverse.origin_shifted(shift)`. Without celestial bodies (flat terrain), volumes shift the scene themselves.
- **Camera clip planes** (`universe/adapt_camera_clip`, on): Godot's culling fails once a camera's far / near passes about 2e7, and a planet seen from orbit needs thousands of kilometers. The current camera's near plane follows its altitude above the nearest body (its own near plane on the ground) and far is 1e7 times that: 5 cm / 500 km on the ground.
- **Settings** (Project Settings, `jar_voxel_terrain/`): `floating_origin/distance`, `universe/adapt_camera_clip`, `universe/time_scale`, `universe/start_time`, `universe/frame_switching`, `gravity/physical_strength` (off: only the dominant body pulls), `gravity/up_sharpness`, `gravity/drive_space_gravity`.

### Water, atmosphere and clouds

Layers of a `JarCelestialBody`: put them under the body (any depth). They read its frame, so they follow its radius, sea level and movement, and find the sun (a `JarBodyLight`'s light, else the first `DirectionalLight3D`, or `sun`) on their own:

- **`JarBodyWater`**: water at the body's sea level, a sphere for planets or a plane for flat worlds. A dense patch follows the camera and is projected onto the exact sea level, so coasts are precise up close. `water.gdshader` colors water by its depth, with waves, shore foam and transparency; under water, `underwater.gdshader` fogs the view by the distance through the water and dims it with depth. `level_offset` raises or lowers it. It is deliberately separate from the volume, so it can be replaced by a simulation later.
- **`JarBodyAtmosphere`**: a physically based atmosphere after Hillaire (2020): Rayleigh and Mie scattering, ozone and multiple scattering, from tables precomputed in C++ for a smooth sphere. Blue sky, orange sunsets, deep blue twilight, distant terrain fading to blue, the planet seen through its air from space. While the camera is inside it, it also lights the scene: the sun's light reaches you through the air (reddening as it sets, gone behind the planet) and the sky's light becomes the ambient light, on top of the Environment's own ambient (starlight). The air (`rayleigh_*`, `mie_*`, `ozone_*`, heights as fractions of `thickness`) is set on the node; `sunset_strength` reddens sunlight on its way in without hazing nearby terrain (a small planet's air is thin even at the horizon).
- **`JarBodyClouds`**: a cloud layer at `altitude` meters above sea level: drifting noise with large-scale weather systems (clear and overcast regions), self-shadowing and a dark night side.

All create their default material at runtime (not saved with the scene); assign your own `material` to tweak or replace the shaders.

The demo's sky (`demo/demo/assets/space/space_sky.gdshader`) draws stars with a realistic brightness distribution and colors, a faint galactic band and the sun's disk, in physical-ish brightness: with auto exposure (the demo environment's camera attributes) the stars disappear behind a bright day sky and come out in space.

### Resolution and level of detail

- `voxel_size` sets the smallest detail (and edit) in meters; `extent` the size of the volume.
- Detail is highest around viewers and falls off in rings: `lod_detail_distance` widens the full-detail region, `lod_shell_size` sets how quickly detail drops after it. Level of detail depends only on viewer and sample positions, and neighbouring chunks never differ by more than one level, so seams stay closed at any setting. Beyond the coarsest level the volume can make chunks for (set by its `extent`), detail stays at that level, so a planet stays visible from far away.

### Meshing

`meshing_algorithm` selects the mesher per volume, and can be switched at runtime:

- **Surface Nets** (default): smooth, rounded surfaces.
- **Dual Contouring**: reproduces sharp edges and corners (stamped boxes, meshes, carved tunnels). Edits store the exact surface normal of the shape they apply, and untouched terrain uses the SDF's gradient, so vertices land on creases and corners; quads are split along sharp edges and creases get their own normals. Seams between LOD levels use the same stitching as surface nets.

`manifold` (works with both meshers) gives every separate piece of surface inside a voxel its own vertex, so thin walls and features smaller than a voxel stay closed, 2-manifold surfaces instead of collapsing into one vertex. It costs no measurable meshing time.

### Tests

Headless checks live in `demo/tests` and exit with a non-zero code on failure:

```bash
godot --headless --path demo res://tests/sdf_tests.tscn
godot --headless --path demo res://tests/edit_tests.tscn
godot --headless --path demo res://tests/mesher_tests.tscn
godot --headless --path demo res://tests/lod_tests.tscn
godot --headless --path demo res://tests/topology_tests.tscn
godot --headless --path demo res://tests/population_tests.tscn
godot --headless --path demo res://tests/debris_tests.tscn
godot --headless --path demo res://tests/universe_tests.tscn
godot --headless --fixed-fps 60 --path demo res://tests/player_tests.tscn
```

### Performance tips

- `performance_collider_distance` — when `> 0`, physics colliders are only generated for chunks within this distance of a viewer. Collider generation is one of the most expensive parts of chunk updates, so setting this to slightly more than your physics interaction range is a large win.
- `performance_collider_budget_ms` — main-thread time per frame spent uploading colliders (nearest first). Raise it to get collision sooner, lower it for steadier frame times.
- `performance_updated_colliders_per_second` — optional hard cap on collider uploads per second; `0` (default) means only the time budget applies.
- Moving volumes (an orbiting moon) with your own gravity `Area3D`: put the terrain on its own physics layer (`collision_layer`) and leave it out of the area's mask. An area that moves with the volume and overlaps its terrain colliders costs a lot of physics time every frame. `JarCelestialBody` gravity needs no areas.
- Chunks without a collider take their body out of the physics space.
- Chunks are recycled through an internal object pool, so heavy LOD churn does not repeatedly allocate/free thousands of nodes.
- Memory is dominated by the octree (about 64 bytes per node, a few million nodes for a planet). Detail the viewer leaves behind is pruned once the coarser chunks are shown and regenerated on return, so memory stays flat while travelling; edited regions are always kept. `voxel_size` has the largest effect: halving it roughly quadruples the nodes near the surface.

## Contributing

Contributions are welcome! Please fork the repository and submit a pull request.

In particular, here are some areas of interest, in no particular order of importance:
- More SDF shapes, operators and terrain generators.
- Navigation system.

## Credits

- The erosion filter in `JarTerrainSdf` is based on Rune Skovbo Johansen's (runevision) [Fast and Gorgeous Erosion Filter](https://blog.runevision.com/2026/03/fast-and-gorgeous-erosion-filter.html), which builds on work by Clay John and Felix Westin (Fewes). It is an independent implementation written from the article.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
