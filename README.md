# Godot Voxel Terrain Plugin

![Banner Image](https://github.com/JorisAR/GDVoxelTerrain/blob/main/banner.png?raw=true)

This project adds a smooth voxel terrain system to godot. 
More precisely, it uses an octree to store an SDF, which is then meshed using a custom version of surface nets. Level of detail systems are in place for large viewing distances.

Terrain is fully editable at runtime (dig / add anywhere), supports planar and spherical (planet) worlds, and can be shaped entirely from data using composable SDF resources.

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

Open the `demo` project in Godot and run `demo/demo.tscn` (spherical planet) or `demo/flat_terrain.tscn` (planar terrain).
Controls: `WASD` + mouse to fly, `left click` to add terrain, `right click` to dig, `scroll` to resize the brush, `F` to force a LOD update.

## Usage

- Please refer to the demo scenes to see how to use the terrain system.
- The essentials for your own scene:
  1. Add a `JarWorld`-derived node (`JarPlanarWorld` or `JarSphericalWorld`).
  2. Add a `JarVoxelTerrain` node as its child.
  3. Assign its `sdf` (any `JarSignedDistanceField` resource — this defines the starting terrain shape), its `chunk_scene` (`addons/jar_voxel_terrain/src/chunk.tscn`), and its `player_node` / `world_node` paths.
- Edit terrain at runtime from scripts with `terrain.sphere_edit(position, radius, union)` or the more general `terrain.modify(...)`.

### Shaping worlds with SDFs

Terrain shape is authored as composable `JarSignedDistanceField` resources:

- **Primitives:** `JarSphereSdf`, `JarBoxSdf`, `JarPlaneSdf`, `JarCylinderSdf`, `JarCapsuleSdf`, `JarTorusSdf`, plus the noise-based `JarTerrainSdf` (heightmap terrain) and `JarPlanetSdf` (planets).
- **`JarOperationSdf`** combines two SDFs with a boolean operation (union, subtraction, intersection, and smooth variants) — e.g. subtract a sphere from terrain to start with a crater. Operations can be nested for full CSG.
- **`JarTransformSdf`** positions, rotates and scales any child SDF.
- Every SDF can be queried from scripts: `sdf.sample(position)` returns the signed distance (negative = inside).

See `demo/demo/op_demo.tscn` + `demo/demo/op_carve.tres` for a worked example (a crater carved out of noise terrain with a single resource).

### Performance tips

- `performance_collider_distance` — when `> 0`, physics colliders are only generated for chunks within this distance of the camera. Collider generation is one of the most expensive parts of chunk updates, so setting this to slightly more than your physics interaction range is a large win.
- `performance_updated_colliders_per_second` — throttles collider updates per frame.
- Chunks are recycled through an internal object pool, so heavy LOD churn does not repeatedly allocate/free thousands of nodes.

## Contributing

Contributions are welcome! Please fork the repository and submit a pull request.

In particular, here are some areas of interest, in no particular order of importance:
- Optimizing LOD connections, i.e. remove the overdraw/duplicate triangle generation.
- Multi-material support: painting materials (4-way weight blend) is in — see `sphere_edit`/`modify`'s `material` argument. Texture-array/triplanar materials and more than 4 slots remain open.
- Multithreaded octree generation (the octree build currently runs on a single managed worker thread).
- Add more interesting SDF options:
    - Better noise based SDFs for more realistic terrain.
    - SDF based on an arbitrary triangle mesh.
- Navigation system.
- Dual contouring extension.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
