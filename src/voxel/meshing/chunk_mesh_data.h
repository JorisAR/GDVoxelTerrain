#ifndef JAR_CHUNK_MESH_DATA_H
#define JAR_CHUNK_MESH_DATA_H

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <algorithm>
#include <memory>
#include <vector>
#include <unordered_map>
#include "bounds.h"
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

using namespace godot;

// The volume SDF's gradients at the samples a chunk was meshed from, kept for
// its next mesh: an edit changes a few samples (and stores their normals), the
// rest are the same, and the terrain's gradient is the costliest part of
// meshing (~17 us each, 10 ms for a chunk of ground). Likewise the
// projections onto the SDF's surface where an edit meets untouched ground. Keyed by the sample's
// center and size, and valid for the SDF and mesh version it was made with.
struct SampleGradients
{
    struct Entry
    {
        glm::vec3 center;
        int size;
        glm::vec3 gradient; // normalized
        float distanceScale;
        // Where an edit meets it: its plane projected onto the SDF's surface
        // (see StitchedSurfaceNets::place_dual_contouring_vertex), if done.
        bool projected;
        glm::vec3 surface;       // volume space
        glm::vec3 surfaceNormal;
        bool operator<(const Entry &o) const
        {
            if (center.x != o.center.x)
                return center.x < o.center.x;
            if (center.y != o.center.y)
                return center.y < o.center.y;
            if (center.z != o.center.z)
                return center.z < o.center.z;
            return size < o.size;
        }
    };
    const void *sdf = nullptr;
    int meshVersion = 0;
    std::vector<Entry> entries; // sorted

    const Entry *find(const glm::vec3 &center, int size) const
    {
        Entry key{center, size};
        const auto it = std::lower_bound(entries.begin(), entries.end(), key);
        return it != entries.end() && !(key < *it) ? &*it : nullptr;
    }
};

class JarVoxelChunk;

class ChunkMeshData
{
  public:
    static const int MaxCollisionLod = 2;

    Array mesh_array;
    // mesh_array uploaded to the GPU ahead of showing it (see
    // MeshComputeScheduler::process): showing it is then just a swap.
    Ref<ArrayMesh> uploaded;

    // Uploads mesh_array into `uploaded` (main thread).
    void upload()
    {
        uploaded.instantiate();
        // CUSTOM0 and CUSTOM1 hold 4 bytes per vertex (materials, biome weights).
        uploaded->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, mesh_array, Array(), Dictionary(),
                                          (Mesh::ARRAY_CUSTOM_RGBA8_UNORM << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT) |
                                              (Mesh::ARRAY_CUSTOM_RGBA8_UNORM << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT));
    }
    PackedVector3Array collision_mesh;
    int lod;
    uint64_t boundaries; // see VoxelOctreeNode::compute_boundaries
    int mesh_version = 0;
    bool edge_chunk;
    Bounds bounds;
    // Painted amount per vertex (0 = ground): details avoid painted ground.
    PackedFloat32Array painted;
    std::shared_ptr<const SampleGradients> gradients; // for the next mesh, if kept
    // The chunk made ready for this mesh ahead of the commit, hidden (see
    // VoxelOctreeNode::prepare_chunk); null when an existing chunk gets it.
    JarVoxelChunk *prepared = nullptr;
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
