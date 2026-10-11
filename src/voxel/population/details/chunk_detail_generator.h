#include <vector>
#include <godot_cpp/classes/multi_mesh.hpp>
// #include <godot_cpp/classes/array.hpp>
// #include <godot_cpp/classes/transform3d.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/templates/hashfuncs.hpp>
// #include <godot_cpp/variant/math.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "terrain_detail.h"
#include "up_field.h"

using namespace godot;

class JarVoxelVolume;
class ChunkMeshData;

class ChunkDetailGenerator {
private:
    PackedVector3Array _verts;
    PackedInt32Array _indices;
    PackedVector3Array _normals;
    PackedColorArray _colors;
    UpField _upField;

    float DeterministicFloat(const Vector3 &input) const {
        // Generate a deterministic float based on the input vector
        uint32_t hash = HashMapHasherDefault::hash(input);
        return static_cast<float>(hash % 10000) / 10000.0f; // Normalize to [0, 1]
    }

public:
    explicit ChunkDetailGenerator(const UpField &upField) : _upField(upField) {}

    // Thread-safe given read-only details: computes instance transforms
    // as MultiMesh buffers, one per detail (see ChunkMeshData::detail_buffers).
    std::vector<PackedFloat32Array> generate_detail_buffers(const TypedArray<JarTerrainDetail> &details,
                                                            const ChunkMeshData &chunkMeshData);
};