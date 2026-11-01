#ifndef JAR_FLOATING_ORIGIN_H
#define JAR_FLOATING_ORIGIN_H

#include <cstdint>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/variant/transform3d.hpp>
#include <unordered_set>

using namespace godot;

// Floating origin: 32-bit floats have about 2 mm of precision at 16 km from
// the origin and 6 cm at 600 km, too coarse near the camera for rendering and
// physics. Once the viewer is more than `distance` (a project setting) from
// the origin, the whole scene is moved by one rigid transform that brings it
// back near the origin. Small worlds never get there: it costs nothing.
//
// With celestial bodies, JarVoxelUniverse does the shift (it moves its view,
// see JarVoxelUniverse::shift_origin); without, the first active volume does
// it (translation only). Scripts that keep scene positions of their own
// connect to JarVoxelUniverse.origin_shifted(transform) and apply it.
namespace FloatingOrigin
{
extern const char *DistanceSetting;
void register_settings();
// Viewer distance from the origin that triggers a shift (0: never).
double distance();

// Moves everything in the tree whose pose does not follow a moved parent
// (root-level and top-level Node3Ds) by `shift`, except the nodes in `skip`
// (placed by someone else). Physics bodies keep their motion: velocities turn
// with the shift.
void apply(Node *root, const Transform3D &shift, const std::unordered_set<uint64_t> &skip);

// Without a universe: shifts when the viewer is beyond distance(), at most
// once per physics frame (any volume may call it). Returns whether it shifted.
bool update_without_universe(Node *context);
} // namespace FloatingOrigin

#endif // JAR_FLOATING_ORIGIN_H
