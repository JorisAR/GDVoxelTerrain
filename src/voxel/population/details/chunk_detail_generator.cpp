#include "chunk_detail_generator.h"
#include "chunk_mesh_data.h"
#include "voxel_volume.h"
#include <cstring>

namespace
{
uint32_t float_bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}
} // namespace

std::vector<PackedFloat32Array> ChunkDetailGenerator::generate_detail_buffers(
    const TypedArray<JarTerrainDetail> &details, const ChunkMeshData &chunkMeshData)
{
    if (details.size() <= 0)
        return {};
    const PackedVector3Array verts = chunkMeshData.mesh_array[Mesh::ARRAY_VERTEX];
    const PackedInt32Array indices = chunkMeshData.mesh_array[Mesh::ARRAY_INDEX];
    const PackedVector3Array normals = chunkMeshData.mesh_array[Mesh::ARRAY_NORMAL];
    const PackedColorArray colors = chunkMeshData.mesh_array[Mesh::ARRAY_COLOR];
    const PackedByteArray custom0 = chunkMeshData.mesh_array[Mesh::ARRAY_CUSTOM0];
    const glm::vec3 chunkCenter = chunkMeshData.bounds.get_center();

    // Painted weight of a vertex: details only grow on unpainted ground.
    auto painted = [&](int v) {
        const Color &c = colors[v];
        float sum = c.r + c.g + c.b + c.a;
        if (custom0.size() >= 4 * (v + 1))
            for (int i = 0; i < 4; ++i)
                sum += custom0[4 * v + i] / 255.0f;
        return sum;
    };

    std::vector<std::vector<Transform3D>> transforms(details.size());
    for (int64_t d = 0; d < details.size(); d++)
    {
        Ref<JarTerrainDetail> detail = details[d];
        if (detail.is_null() || detail->get_density() <= 0.0f)
            continue;
        const float density = detail->get_density();
        for (int64_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const int ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
            const Vector3 a = verts[ia], b = verts[ib], c = verts[ic];
            const Vector3 e1 = b - a, e2 = c - a;
            const float area = e1.cross(e2).length() * 0.5f;
            // Seeded by the triangle's position (not its index), so the same
            // surface gets the same details however the chunk was meshed.
            const Vector3 centroid = (a + b + c) / 3.0f + Vector3(chunkCenter.x, chunkCenter.y, chunkCenter.z);
            PopulationRandom rng(PopulationRandom::hash(float_bits(centroid.x), float_bits(centroid.y),
                                                        float_bits(centroid.z), uint32_t(d)));
            const float expected = area * density;
            int count = int(expected);
            count += rng.next() < expected - count ? 1 : 0;
            for (int j = 0; j < count; j++)
            {
                float r1 = rng.next(), r2 = rng.next();
                if (r1 + r2 >= 1.0f)
                {
                    r1 = 1.0f - r1;
                    r2 = 1.0f - r2;
                }
                const float r0 = 1.0f - r1 - r2;
                const float rotation = rng.next(), scale = rng.next();
                if (r0 * painted(ia) + r1 * painted(ib) + r2 * painted(ic) > 0.5f)
                    continue;
                const Vector3 local = a + e1 * r1 + e2 * r2;
                const glm::vec3 position = glm::vec3(local.x, local.y, local.z) + chunkCenter;
                const Vector3 n = (normals[ia] * r0 + normals[ib] * r1 + normals[ic] * r2).normalized();
                const glm::vec3 normal(n.x, n.y, n.z);
                const glm::vec3 up = _upField.up(position);
                if (!detail->accepts(position, _upField.height(position), normal, up))
                    continue;
                // Chunk-relative: the MultiMesh lives under the chunk node.
                Transform3D t = detail->build_transform(position, up, normal, rotation, scale);
                t.origin -= Vector3(chunkCenter.x, chunkCenter.y, chunkCenter.z);
                transforms[d].push_back(t);
            }
        }
    }

    std::vector<PackedFloat32Array> buffers(details.size());
    for (size_t d = 0; d < transforms.size(); d++)
    {
        PackedFloat32Array &buffer = buffers[d];
        buffer.resize(transforms[d].size() * 12);
        float *w = buffer.ptrw();
        for (const Transform3D &t : transforms[d])
        {
            // MultiMesh TRANSFORM_3D layout: basis rows interleaved with origin.
            for (int row = 0; row < 3; row++)
            {
                *w++ = t.basis.rows[row].x;
                *w++ = t.basis.rows[row].y;
                *w++ = t.basis.rows[row].z;
                *w++ = t.origin[row];
            }
        }
    }
    return buffers;
}
