#ifndef JAR_VOXEL_VIEWER_H
#define JAR_VOXEL_VIEWER_H

#include <godot_cpp/classes/node3d.hpp>
#include <algorithm>
#include <vector>

using namespace godot;

// Marks a point of interest (player, camera, server-side client) that voxel
// volumes refine their level of detail around. Every volume in the scene uses
// all enabled viewers, converted into its own local space, so volumes can move
// and rotate freely. Without any viewer, volumes fall back to the active camera.
//
// With `predict_motion`, volumes also refine around where the viewer will be
// `predict_seconds` from now at its smoothed velocity, on top of where it is,
// so terrain is ready on arrival and a sudden turn loses nothing nearby.
class JarVoxelViewer : public Node3D
{
    GDCLASS(JarVoxelViewer, Node3D);

  private:
    bool _enabled = true;
    bool _predictMotion = false;
    float _predictSeconds = 1.0f;
    float _predictMaxDistance = 0.0f; // meters, 0: no limit
    static std::vector<JarVoxelViewer *> s_viewers; // main thread only

  protected:
    static void _bind_methods();
    void _notification(int what);

  public:
    bool is_enabled() const { return _enabled; }
    void set_enabled(bool value) { _enabled = value; }
    bool get_predict_motion() const { return _predictMotion; }
    void set_predict_motion(bool value) { _predictMotion = value; }
    float get_predict_seconds() const { return _predictSeconds; }
    void set_predict_seconds(float value) { _predictSeconds = std::max(0.0f, value); }
    float get_predict_max_distance() const { return _predictMaxDistance; }
    void set_predict_max_distance(float value) { _predictMaxDistance = std::max(0.0f, value); }

    // Viewers currently inside the scene tree (enabled or not).
    static const std::vector<JarVoxelViewer *> &get_viewers() { return s_viewers; }
};

#endif // JAR_VOXEL_VIEWER_H
