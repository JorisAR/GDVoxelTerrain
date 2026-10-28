#ifndef JAR_VOLUME_EXTENSION_H
#define JAR_VOLUME_EXTENSION_H

class JarVoxelVolume;
struct ModifySettings;

// Optional behaviour plugged into a volume (JarVoxelVolume::add_extension),
// e.g. debris (JarVoxelDebris). The volume calls these at well-defined points
// of its update; an extension that is not added costs nothing, and removing
// one needs no change to the volume.
class VolumeExtension
{
  public:
    virtual ~VolumeExtension() = default;

    // Main thread, every frame (also while the volume builds).
    virtual void process(JarVoxelVolume &volume, double delta)
    {
    }

    // Build worker, right after an edit changed the octree (values settled).
    // The octree is safe to read here (e.g. JarVoxelVolume::sample_region) and
    // must not be changed; hand results to the main thread.
    virtual void edit_applied(JarVoxelVolume &volume, const ModifySettings &edit)
    {
    }

    // Main thread, right after a batch of new chunk meshes was shown at once
    // (every chunk an applied edit changed is up to date on screen now).
    virtual void meshes_committed(JarVoxelVolume &volume)
    {
    }
};

#endif // JAR_VOLUME_EXTENSION_H
