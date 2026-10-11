#ifndef JAR_VOXEL_VIEWER_H
#define JAR_VOXEL_VIEWER_H

#include <godot_cpp/classes/node3d.hpp>
#include <vector>

using namespace godot;

// Marks a point of interest (player, camera, server-side client) that voxel
// volumes refine their level of detail around. Every volume in the scene uses
// all enabled viewers, converted into its own local space, so volumes can move
// and rotate freely. Without any viewer, volumes fall back to the active camera.
class JarVoxelViewer : public Node3D
{
    GDCLASS(JarVoxelViewer, Node3D);

  private:
    bool _enabled = true;
    static std::vector<JarVoxelViewer *> s_viewers; // main thread only

  protected:
    static void _bind_methods();
    void _notification(int what);

  public:
    bool is_enabled() const { return _enabled; }
    void set_enabled(bool value) { _enabled = value; }

    // Viewers currently inside the scene tree (enabled or not).
    static const std::vector<JarVoxelViewer *> &get_viewers() { return s_viewers; }
};

#endif // JAR_VOXEL_VIEWER_H
