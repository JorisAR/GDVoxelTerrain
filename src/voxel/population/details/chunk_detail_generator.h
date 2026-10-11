#ifndef JAR_CHUNK_DETAIL_GENERATOR_H
#define JAR_CHUNK_DETAIL_GENERATOR_H

#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <vector>

#include "terrain_detail.h"
#include "up_field.h"

using namespace godot;

class ChunkMeshData;

// Scatters details over a chunk mesh (mesh thread).
class ChunkDetailGenerator
{
  private:
    UpField _upField;

  public:
    explicit ChunkDetailGenerator(const UpField &upField) : _upField(upField) {}

    // Thread-safe given read-only details: one MultiMesh buffer per detail
    // (see ChunkMeshData::detail_buffers), with chunk-relative transforms.
    std::vector<PackedFloat32Array> generate_detail_buffers(const TypedArray<JarTerrainDetail> &details,
                                                            const ChunkMeshData &chunkMeshData);
};

#endif // JAR_CHUNK_DETAIL_GENERATOR_H
