#!/usr/bin/env python
import os
import sys
# from scons_compiledb import compile_db # Import the compile_db function # Call the compile_db function to enable compile_commands.json generation
# compile_db()

# Import the SConstruct from godot-cpp
env = SConscript("godot-cpp/SConstruct")

# Add necessary include directories
env.Append(CPPPATH=[
    "src/glm",
    "src/utility",
    "src",
    "src/sdf",
    "src/world",
    "src/world/storage",
    "src/world/voxels",
    "src/world/voxels/meshing",
    "src/world/population",
    "src/world/population/details",
    "src/world/population/features",
])

## Add main source files
sources = Glob("src/*.cpp") + Glob("src/utility/*.cpp") + Glob("src/sdf/*.cpp") + \
    Glob("src/world/*.cpp") + Glob("src/world/storage/*.cpp") + Glob("src/world/voxels/*.cpp") + Glob("src/world/voxels/meshing/*.cpp") + \
    Glob("src/world/population/*.cpp") + Glob("src/world/population/details/*.cpp") + \
    Glob("src/world/population/features/*.cpp")

if env["target"] != "template_release":
	try:
		doc_data = env.GodotCPPDocData("src/gen/doc_data.gen.cpp", source=Glob("doc_classes/*.xml"))
		sources.append(doc_data)
	except AttributeError:
		print("Not including class reference as we're targeting a pre-4.3 baseline.")

# #compiler flags
# if env['PLATFORM'] == 'windows':
#     if env['CXX'] == 'x86_64-w64-mingw32-g++':
#         env.Append(CXXFLAGS=['-std=c++11'])  # Example flags for MinGW
#     elif env['CXX'] == 'cl':
#         env.Append(CXXFLAGS=['/EHsc'])  # Apply /EHsc for MSVC

if env.get("is_msvc", False):
    env.Append(CXXFLAGS=['/std:c++17'])

# Handle different platforms
if env["platform"] == "macos":
    mac_env = env.Clone()
    mac_env["SHLIBPREFIX"] = ""
    library = mac_env.SharedLibrary(
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
