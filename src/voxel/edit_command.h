#ifndef JAR_EDIT_COMMAND_H
#define JAR_EDIT_COMMAND_H

#include "signed_distance_field.h"
#include <godot_cpp/variant/variant.hpp>

using namespace godot;

// An edit as plain data (see JarVoxelVolume::edit_made), so it can go over a
// network or into a replay. A brush saved as a file is its path; one made in
// memory is its class and properties. Decoding only ever makes SDFs and noises
// or loads res:// files: commands may come from other players.
namespace EditCommand
{
// Nil if it cannot be written down (a class that may not be sent).
Variant encode_sdf(const Ref<JarSignedDistanceField> &sdf);
// Null if the data is not an SDF.
Ref<JarSignedDistanceField> decode_sdf(const Variant &data);
} // namespace EditCommand

#endif // JAR_EDIT_COMMAND_H
