#include "register_types.h"
#include "box_sdf.h"
#include "capsule_sdf.h"
#include "cylinder_sdf.h"
#include "operation_sdf.h"
#include "torus_sdf.h"
#include "transform_sdf.h"
#include "planar_world.h"
#include "plane_sdf.h"
#include "planet_sdf.h"
#include "sphere_sdf.h"
#include "spherical_world.h"
#include "terrain_detail.h"
#include "terrain_populator.h"
#include "terrain_sdf.h"
#include "voxel_chunk.h"
#include "voxel_terrain.h"
#include "world.h"
using namespace godot;

void initialize_jar_voxel_terrain_module(ModuleInitializationLevel p_level)
{
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE)
    {
        // SDFs
        GDREGISTER_ABSTRACT_CLASS(JarSignedDistanceField);
        GDREGISTER_CLASS(JarBoxSdf);
        GDREGISTER_CLASS(JarSphereSdf);
        GDREGISTER_CLASS(JarPlaneSdf);
        GDREGISTER_CLASS(JarTerrainSdf);
        GDREGISTER_CLASS(JarPlanetSdf);
        GDREGISTER_CLASS(JarOperationSdf);
        GDREGISTER_CLASS(JarTorusSdf);
        GDREGISTER_CLASS(JarCylinderSdf);
        GDREGISTER_CLASS(JarCapsuleSdf);
        GDREGISTER_CLASS(JarTransformSdf);

        // WORLD
        GDREGISTER_ABSTRACT_CLASS(JarWorld);
        GDREGISTER_CLASS(JarPlanarWorld);
        GDREGISTER_CLASS(JarSphericalWorld);

        // POPULATION
        GDREGISTER_ABSTRACT_CLASS(JarTerrainPopulator);
        GDREGISTER_CLASS(JarTerrainDetail);

        // TERRAIN
        GDREGISTER_CLASS(JarVoxelTerrain);
        GDREGISTER_CLASS(JarVoxelChunk);
    }
}

void uninitialize_jar_voxel_terrain_module(ModuleInitializationLevel p_level)
{
    if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE)
    {
    }
}

extern "C"
{
    // Initialization.
    GDExtensionBool GDE_EXPORT jar_voxel_terrain_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                                                              const GDExtensionClassLibraryPtr p_library,
                                                              GDExtensionInitialization *r_initialization)
    {
        godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

        init_obj.register_initializer(initialize_jar_voxel_terrain_module);
        init_obj.register_terminator(uninitialize_jar_voxel_terrain_module);
        init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

        return init_obj.init();
    }
}