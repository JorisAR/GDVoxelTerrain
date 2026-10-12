#include "chunk_detail_generator.h"
#include "chunk_mesh_data.h"
#include "population_rendering.h"
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
    const std::vector<Ref<JarTerrainDetail>> &details, const std::vector<int> &detailBiomes,
    const Ref<JarBiomeMap> &biomeMap, const std::shared_ptr<const GroundPalette> &ground,
    const ChunkMeshData &chunkMeshData)
{
    if (details.empty())
        return {};
    const PackedVector3Array verts = chunkMeshData.mesh_array[Mesh::ARRAY_VERTEX];
    const PackedInt32Array indices = chunkMeshData.mesh_array[Mesh::ARRAY_INDEX];
    const PackedVector3Array normals = chunkMeshData.mesh_array[Mesh::ARRAY_NORMAL];
    const PackedFloat32Array &paintedAmount = chunkMeshData.painted;
    const glm::vec3 chunkCenter = chunkMeshData.bounds.get_center();

    // Painted weight of a vertex: details only grow on unpainted ground.
    auto painted = [&](int v) { return v < paintedAmount.size() ? paintedAmount[v] : 0.0f; };

    std::vector<std::vector<Transform3D>> transforms(details.size());
    std::vector<std::vector<glm::vec3>> colors(details.size());
    for (int64_t d = 0; d < int64_t(details.size()); d++)
    {
        Ref<JarTerrainDetail> detail = details[d];
        const int biome = d < int64_t(detailBiomes.size()) ? detailBiomes[d] : -1;
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
                const float rotation = rng.next(), scale = rng.next(), keep = rng.next();
                if (r0 * painted(ia) + r1 * painted(ib) + r2 * painted(ic) > 0.5f)
                    continue;
                const Vector3 local = a + e1 * r1 + e2 * r2;
                const glm::vec3 position = glm::vec3(local.x, local.y, local.z) + chunkCenter;
                const Vector3 n = (normals[ia] * r0 + normals[ib] * r1 + normals[ic] * r2).normalized();
                const glm::vec3 normal(n.x, n.y, n.z);
                const glm::vec3 up = _upField.up(position);
                float chance = detail->probability(position, _upField.height(position), normal, up);
                if (biome >= 0 && biomeMap.is_valid())
                    chance *= biomeMap->evaluate(position, _upField).weight_of(biome);
                if (keep >= chance)
                    continue;
                // Chunk-relative: the MultiMesh lives under the chunk node.
                Transform3D t = detail->build_transform(position, up, normal, rotation, scale);
                t.origin -= Vector3(chunkCenter.x, chunkCenter.y, chunkCenter.z);
                transforms[d].push_back(t);
                colors[d].push_back(ground ? ground->color_at(position, _upField) : glm::vec3(0.3f, 0.35f, 0.2f));
            }
        }
    }

    std::vector<PackedFloat32Array> buffers(details.size());
    for (size_t d = 0; d < transforms.size(); d++)
    {
        PackedFloat32Array &buffer = buffers[d];
        buffer.resize(transforms[d].size() * Stride);
        float *w = buffer.ptrw();
        for (size_t i = 0; i < transforms[d].size(); i++)
        {
            const Transform3D &t = transforms[d][i];
            // MultiMesh TRANSFORM_3D layout: basis rows interleaved with origin.
            for (int row = 0; row < 3; row++)
            {
                *w++ = t.basis.rows[row].x;
                *w++ = t.basis.rows[row].y;
                *w++ = t.basis.rows[row].z;
                *w++ = t.origin[row];
            }
            // Custom data: the ground color; the chunk fills in the LOD range.
            PopulationRendering::write_custom(w, colors[d][i], 0.0f, 0.0f);
            w += 4;
        }
    }
    return buffers;
}
