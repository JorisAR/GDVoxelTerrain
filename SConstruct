#!/usr/bin/env python
import os
import sys
# from scons_compiledb import compile_db # Import the compile_db function # Call the compile_db function to enable compile_commands.json generation
# compile_db()

# Import the SConstruct from godot-cpp
env = SConscript("godot-cpp/SConstruct")

# Add necessary include directories
env.Append(CPPPATH=[
    "src/glm/",
    "src/utility/",
    "src/",
    "src/sdf/",
    "src/voxel/",
    "src/voxel/meshing",
    "src/voxel/meshing/stitched_surface_nets",
    "src/voxel/population",
    "src/voxel/population/details",
])

sources = (
    Glob("src/*.cpp")
    + Glob("src/utility/*.cpp")
    + Glob("src/sdf/*.cpp")
    + Glob("src/voxel/*.cpp")
    + Glob("src/voxel/meshing/*.cpp")
    + Glob("src/voxel/meshing/stitched_surface_nets/*.cpp")
    + Glob("src/voxel/population/*.cpp")
    + Glob("src/voxel/population/details/*.cpp")
)

if env["target"] != "template_release":
	try:
		doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
		sources.append(doc_data)
	except AttributeError:
		print("Not including class reference as we're targeting a pre-4.3 baseline.")

# C++17 is set by godot-cpp for every compiler (tools/common_compiler_flags.py).

# Handle different platforms
if env["platform"] == "macos":
    library = env.SharedLibrary(
        "demo/addons/jar_voxel_terrain/bin/jar_voxel_terrain.{}.{}.framework/jar_voxel_terrain.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
elif env["platform"] == "ios":
    if env["ios_simulator"]:
        library = env.StaticLibrary(
            "demo/addons/jar_voxel_terrain/bin/jar_voxel_terrain.{}.{}.simulator.a".format(env["platform"], env["target"]),
            source=sources,
        )
    else:
        library = env.StaticLibrary(
            "demo/addons/jar_voxel_terrain/bin/jar_voxel_terrain.{}.{}.a".format(env["platform"], env["target"]),
            source=sources,
        )
else:
    library = env.SharedLibrary(
        "demo/addons/jar_voxel_terrain/bin/jar_voxel_terrain{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)
