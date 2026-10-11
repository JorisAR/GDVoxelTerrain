#ifndef JAR_JOB_SYSTEM_H
#define JAR_JOB_SYSTEM_H

#include "thread_pool.h"

// Process-wide worker pool shared by every JarVoxelVolume, so a scene with many
// volumes (planets, moons, props) does not spawn a thread pool per volume.
// Created on first use; joined when the extension unloads.
namespace JobSystem
{
ThreadPool &get_pool();
void shutdown();
} // namespace JobSystem

#endif // JAR_JOB_SYSTEM_H
