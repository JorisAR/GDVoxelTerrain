#ifndef JAR_VOLUME_EXTENSION_H
#define JAR_VOLUME_EXTENSION_H

class JarVoxelChunk;
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

    // A pool thread, right after an edit changed the octree (values settled),
    // while its chunks are meshed. The octree is safe to read here (e.g.
    // JarVoxelVolume::sample_region) and must not be changed; hand results to
    // the main thread. Edits are passed one at a time, in order.
    virtual void edit_applied(JarVoxelVolume &volume, const ModifySettings &edit)
    {
    }

    // Main thread, right after a batch of new chunk meshes was shown at once
    // (every chunk an applied edit changed is up to date on screen now).
    virtual void meshes_committed(JarVoxelVolume &volume)
    {
    }

    // Main thread, as an edit is queued (before it is applied). `reach`: how
    // far beyond its shape it changes the surface.
    virtual void edit_queued(JarVoxelVolume &volume, const ModifySettings &edit, float reach)
    {
    }

    // Main thread: a full-detail chunk got a new mesh (chunk->detail_inputs).
    virtual void chunk_meshed(JarVoxelVolume &volume, JarVoxelChunk *chunk)
    {
    }

    // Main thread: a chunk is no longer full detail, or was parked.
    virtual void chunk_gone(JarVoxelVolume &volume, JarVoxelChunk *chunk)
    {
    }

    // Main thread: biomes or colliders changed; what was made from them is outdated.
    virtual void population_invalidated(JarVoxelVolume &volume)
    {
    }

    // Main thread: the volume is about to free its chunks and octree (editor
    // preview off), though the volume itself stays.
    virtual void volume_cleared(JarVoxelVolume &volume)
    {
    }

    // Still generating something is_generating_details() should report.
    virtual bool is_generating(const JarVoxelVolume &volume) const
    {
        return false;
    }
};

#endif // JAR_VOLUME_EXTENSION_H
