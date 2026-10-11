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

- `walk_planet.tscn` / `walk_flat.tscn`: walk around as a first-person character (dual contouring meshing).
- `demo.tscn` (planet), `moon_demo.tscn` (a planet with an orbiting, spinning moon), `flat_terrain.tscn`, `op_demo.tscn`: fly around with a free camera.

Controls:

- Walking scenes: `WASD` move, `Space` jump, `Shift` sprint, `Tab` toggle flying (`Space`/`Shift` up/down, `Ctrl` + scroll speed).
- Free camera scenes: `WASD` + mouse to fly, `Space`/`Shift` up/down, `Ctrl` + scroll speed, `F` force a LOD update.
- Editing (all scenes): `left click` dig, `right click` add, `Q` next brush (sphere, box, cone, prism, torus), `scroll` brush size, `E` smooth blending, `1`-`4` material, `R` toggle the brush preview. `Esc` releases the mouse.

The editing scripts in `demo/demo/editing` are meant to be reused: `SdfEditor` edits with any SDF resource you assign (plus an optional preview mesh), `BrushEditor` adds the built-in brushes. `demo/demo/player` contains the character controller, which follows Godot's gravity (an `Area3D` with point gravity makes a planet).

## Usage

- Please refer to the demo scenes to see how to use the terrain system.
- The essentials for your own scene:
  1. Add a `JarVoxelVolume` node anywhere in the scene (it can be moved, rotated or animated).
  2. Assign its `sdf` (any `JarSignedDistanceField` resource — this defines the starting shape) and its `chunk_scene` (`addons/jar_voxel_terrain/src/chunk.tscn`).
  3. For planets set `up_mode` to *Radial* and `sea_level` to the planet radius; flat worlds keep the default *Axis* mode. This drives population rules and `get_up_direction_at()` / `get_height_at()`.
  4. Add a `JarVoxelViewer` as a child of your player or camera. Without one, volumes follow the active camera.
- Edit volumes at runtime from scripts with `volume.sphere_edit(position, radius, union)` or the more general `volume.modify(...)`. To edit whatever a physics raycast hit: `hit.collider.get_parent().get_volume()`.

### Migrating from 0.x (`JarVoxelTerrain`)

- `JarVoxelTerrain` is now `JarVoxelVolume`.
- `player_node` is replaced by `JarVoxelViewer` nodes (add one under your player/camera).
- `JarWorld`, `JarPlanarWorld` and `JarSphericalWorld` are gone: use a plain `Node3D` and set the volume's `up_mode`, `up_center` and `sea_level` (radius) instead. For gravity, use Godot's `Area3D` point gravity.
- `performance_max_concurrent_tasks` is gone: all volumes share one worker pool sized to the CPU.
- Operation constants are now `JarVoxelVolume.SDF_OPERATION_UNION` etc.

### Shaping worlds with SDFs

Shapes are authored as composable `JarSignedDistanceField` resources:

- **Primitives:** `JarSphereSdf`, `JarBoxSdf` (optionally rounded), `JarPlaneSdf`, `JarCylinderSdf`, `JarCapsuleSdf`, `JarTorusSdf`, `JarConeSdf`, `JarEllipsoidSdf`.
- **Terrain:** `JarTerrainSdf` (noise heightmap) and `JarPlanetSdf` (noisy sphere).
- **Meshes:** `JarMeshSdf` turns any closed triangle mesh into an SDF. It is baked once into a distance grid stored in the resource (press *Bake* in the inspector, or it bakes on first use).
- **Combining:** `JarOperationSdf` (union / subtraction / intersection and smooth variants, nestable for full CSG) and `JarTransformSdf` (position / rotate / scale a child).
- **Modifiers:** `JarOffsetSdf` (grow, shrink, hollow into a shell), `JarRepeatSdf` (grid copies), `JarDisplaceSdf` (noise displacement).
- Every SDF can be queried from scripts: `sdf.sample(position)`, `sdf.sample_normal(position)`, `sdf.get_bounds()`.

Edit volumes with any bounded SDF, placed with a full transform:

```gdscript
var box := JarBoxSdf.new()
box.extent = Vector3(2, 4, 2)
volume.edit(box, JarVoxelVolume.SDF_OPERATION_UNION, Transform3D(Basis(Vector3.UP, PI / 4), hit_position))
```

In the demos, `Q` cycles the brush (sphere, box, cone, prism mesh) and `E` toggles smooth blending. See `demo/demo/op_demo.tscn` + `demo/demo/op_carve.tres` for a crater carved out of noise terrain with a single resource.

### Meshing

`meshing_algorithm` selects the mesher per volume, and can be switched at runtime:

- **Surface Nets** (default): smooth, rounded surfaces.
- **Dual Contouring**: reproduces sharp edges and corners (stamped boxes, meshes, carved tunnels). Edits store the exact surface normal of the shape they apply, and untouched terrain uses the SDF's gradient, so vertices land exactly on creases and corners. Seams between LOD levels use the same stitching as surface nets.

### Tests

Headless checks live in `demo/tests` and exit with a non-zero code on failure:

```bash
godot --headless --path demo res://tests/sdf_tests.tscn
godot --headless --path demo res://tests/edit_tests.tscn
godot --headless --path demo res://tests/mesher_tests.tscn
godot --headless --fixed-fps 60 --path demo res://tests/player_tests.tscn
```

### Performance tips

- `performance_collider_distance` — when `> 0`, physics colliders are only generated for chunks within this distance of a viewer. Collider generation is one of the most expensive parts of chunk updates, so setting this to slightly more than your physics interaction range is a large win.
- `performance_collider_budget_ms` — main-thread time per frame spent uploading colliders (nearest first). Raise it to get collision sooner, lower it for steadier frame times.
- `performance_updated_colliders_per_second` — optional hard cap on collider uploads per second; `0` (default) means only the time budget applies.
- Chunks are recycled through an internal object pool, so heavy LOD churn does not repeatedly allocate/free thousands of nodes.

## Contributing

Contributions are welcome! Please fork the repository and submit a pull request.

In particular, here are some areas of interest, in no particular order of importance:
- Optimizing LOD connections, i.e. remove the overdraw/duplicate triangle generation.
- Multi-material support: painting materials (4-way weight blend) is in — see `sphere_edit`/`modify`'s `material` argument. Texture-array/triplanar materials and more than 4 slots remain open.
- Add more interesting SDF options:
    - Better noise based SDFs for more realistic terrain.
    - SDF based on an arbitrary triangle mesh.
- Navigation system.
- Dual contouring extension.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
