#ifndef JAR_CHUNK_MESH_DATA_H
#define JAR_CHUNK_MESH_DATA_H

#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <vector>
#include <unordered_map>
#include "bounds.h"
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

using namespace godot;

class ChunkMeshData
{
  public:
    static const int MaxCollisionLod = 2;

    Array mesh_array;
    PackedVector3Array collision_mesh;
    int lod;
    uint16_t boundaries;
    int mesh_version = 0;
    bool edge_chunk;
    Bounds bounds;
    // One MultiMesh buffer (TRANSFORM_3D layout, 12 floats per instance) per
    // terrain detail, filled on the mesh thread. Empty when the chunk shows none.
    std::vector<PackedFloat32Array> detail_buffers;
    // Painted amount per vertex (0 = ground): details avoid painted ground.
    PackedFloat32Array painted;
    // ChunkDetailData chunk_detail_data;

    bool has_collision_mesh() const
    {
        return true; // lod <= MaxCollisionLod;
    }

    ChunkMeshData(const Array &mesh_array, int lod, bool edge_chunk, const Bounds &chunk_bounds)
        : mesh_array(mesh_array), lod(lod), edge_chunk(edge_chunk), bounds(chunk_bounds)
    {
        if (has_collision_mesh())
        {
            // collision_mesh = get_collision_mesh();
        }
    }

    // void instantiate_details() {
    //     chunk_detail_data = ChunkDetailData(this);
    // }

    // Fills collision_mesh (one vertex per triangle corner, as set_faces expects).
    // Called on the mesh thread so only the physics upload is left for the main thread.
    void build_collision_mesh() {
        const PackedVector3Array verts = mesh_array[Mesh::ARRAY_VERTEX];
        const PackedInt32Array indices = mesh_array[Mesh::ARRAY_INDEX];
        collision_mesh.resize(indices.size());
        Vector3 *w = collision_mesh.ptrw();
        const Vector3 *v = verts.ptr();
        const int32_t *idx = indices.ptr();
        for (int64_t i = 0; i < indices.size(); i++)
            w[i] = v[idx[i]];
    }

    // bool should_have_grass_texture() const {
    //     return chunk_detail_data.should_have_grass_texture();
    // }
};

#endif // JAR_CHUNK_MESH_DATA_H
