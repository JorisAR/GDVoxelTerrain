#ifndef JAR_CHUNK_DETAIL_GENERATOR_H
#define JAR_CHUNK_DETAIL_GENERATOR_H

#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <vector>

#include "terrain_detail.h"
#include "biome_map.h"
#include "ground_palette.h"
#include "up_field.h"
#include <memory>

using namespace godot;

// What details grow on: a full-detail chunk's mesh (chunk-relative vertices).
// The arrays share the chunk mesh's data (copy on write), so keeping them is cheap.
struct DetailInputs
{
    PackedVector3Array vertices;
    PackedVector3Array normals;
    PackedInt32Array indices;
    PackedFloat32Array painted; // painted amount per vertex: details avoid painted ground
    glm::vec3 center{0.0f};     // the chunk's center in the volume
    bool valid() const { return !indices.is_empty(); }
};

// Scatters details over a chunk mesh (on the detail thread).
class ChunkDetailGenerator
{
  private:
    UpField _upField;

  public:
    explicit ChunkDetailGenerator(const UpField &upField) : _upField(upField) {}

    // Floats per instance in the buffers: a TRANSFORM_3D transform, then the
    // custom data (PopulationRendering::write_custom; the LOD range is the
    // chunk's to set).
    static constexpr int Stride = 16;

    // Thread-safe given read-only details: one MultiMesh buffer per detail,
    // with chunk-relative transforms. details[i] grows everywhere for
    // biomes[i] == -1, else weighted by that biome.
    std::vector<PackedFloat32Array> generate_detail_buffers(const std::vector<Ref<JarTerrainDetail>> &details,
                                                            const std::vector<int> &detailBiomes,
                                                            const Ref<JarBiomeMap> &biomeMap,
                                                            const std::shared_ptr<const GroundPalette> &ground,
                                                            const DetailInputs &inputs) const;
};

#endif // JAR_CHUNK_DETAIL_GENERATOR_H
