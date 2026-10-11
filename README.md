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

Open the `demo` project in Godot and run `demo/demo.tscn` (planet), `demo/moon_demo.tscn` (a planet with an orbiting, spinning moon) or `demo/flat_terrain.tscn` (flat terrain).
Controls: `WASD` + mouse to fly, `left click` to add terrain, `right click` to dig, `scroll` to resize the brush, `F` to force a LOD update.

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

Terrain shape is authored as composable `JarSignedDistanceField` resources:

- **Primitives:** `JarSphereSdf`, `JarBoxSdf`, `JarPlaneSdf`, `JarCylinderSdf`, `JarCapsuleSdf`, `JarTorusSdf`, plus the noise-based `JarTerrainSdf` (heightmap terrain) and `JarPlanetSdf` (planets).
- **`JarOperationSdf`** combines two SDFs with a boolean operation (union, subtraction, intersection, and smooth variants) — e.g. subtract a sphere from terrain to start with a crater. Operations can be nested for full CSG.
- **`JarTransformSdf`** positions, rotates and scales any child SDF.
- Every SDF can be queried from scripts: `sdf.sample(position)` returns the signed distance (negative = inside).

See `demo/demo/op_demo.tscn` + `demo/demo/op_carve.tres` for a worked example (a crater carved out of noise terrain with a single resource).

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
