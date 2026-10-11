#include "chunk_detail_generator.h"
#include "chunk_mesh_data.h"
#include "voxel_volume.h"

std::vector<PackedFloat32Array> ChunkDetailGenerator::generate_detail_buffers(
    const TypedArray<JarTerrainDetail> &details, const ChunkMeshData &chunkMeshData)
{
    _verts = chunkMeshData.mesh_array[static_cast<int>(Mesh::ArrayType::ARRAY_VERTEX)];
    _indices = chunkMeshData.mesh_array[static_cast<int>(Mesh::ArrayType::ARRAY_INDEX)];
    _normals = chunkMeshData.mesh_array[static_cast<int>(Mesh::ArrayType::ARRAY_NORMAL)];
    _colors = chunkMeshData.mesh_array[static_cast<int>(Mesh::ArrayType::ARRAY_COLOR)];

    glm::vec3 chunkCenterGLM = chunkMeshData.bounds.get_center();
    Vector3 chunkCenter = Vector3(chunkCenterGLM.x, chunkCenterGLM.y, chunkCenterGLM.z);
    if (details.size() <= 0)
        return {};

    std::vector<std::vector<Transform3D>> ts(details.size());
    for (size_t i = 0; i < _indices.size(); i += 3)
    {
        Vector3 posA = _verts[_indices[i]];
        Vector3 posB = _verts[_indices[i + 1]];
        Vector3 posC = _verts[_indices[i + 2]];

        Vector3 edge1 = posB - posA;
        Vector3 edge2 = posC - posA;
        float area = edge1.cross(edge2).length() * 0.5f;

        for (size_t d = 0; d < details.size(); d++)
        {
            Ref<JarTerrainDetail> detail = details[d];
            float n = area * detail->get_density();
            int nInt = static_cast<int>(std::floor(n));
            float nFloat = DeterministicFloat((posA + posB + posC) * (1 + d));
            nInt += (nFloat < (n - nInt)) ? 1 : 0;
            // nInt = 1;

            for (int j = 0; j < nInt; j++)
            {
                float r1 = DeterministicFloat(posA * (1 + d));
                float r2 = DeterministicFloat(posB * (1 + d));
                if (r1 + r2 >= 1)
                {
                    r1 = 1 - r1;
                    r2 = 1 - r2;
                }

                Vector3 position = posA + r1 * edge1 + r2 * edge2;
                // Volume-local position (chunk meshes are relative to the chunk center).
                const Vector3 localPosition = position + chunkCenter;
                const glm::vec3 p(localPosition.x, localPosition.y, localPosition.z);
                const glm::vec3 u = _upField.up(p);
                Vector3 world_up(u.x, u.y, u.z);
                float height = _upField.height(p);


                if (!detail->is_height_in_range(height))
                    continue;

                Vector3 up = _normals[_indices[i]] + r1 * (_normals[_indices[i + 1]] - _normals[_indices[i]]) +
                             r2 * (_normals[_indices[i + 2]] - _normals[_indices[i]]);
                Color color = _colors[_indices[i]] + r1 * (_colors[_indices[i + 1]] - _colors[_indices[i]]) +
                              r2 * (_colors[_indices[i + 2]] - _colors[_indices[i]]);
                up = up.normalized();

                float dot = up.dot(world_up);

                if (detail->is_slope_in_range(dot) || color.r > 0.5)
                    continue;

                if (!detail->get_align_with_normal())
                    up = world_up;

                float r3 = DeterministicFloat(posC * (1 + d));
                float r4 = DeterministicFloat((posA + posC) * (1 + d));

                ts[d].emplace_back(
                    detail->build_transform(position, up, r3 * 2 * Math_PI,
                                            Math::lerp(detail->get_minimum_scale(), detail->get_maximum_scale(), r4)));
            }
        }
    }

    std::vector<PackedFloat32Array> buffers(details.size());
    for (size_t d = 0; d < details.size(); d++)
    {
        PackedFloat32Array &buffer = buffers[d];
        buffer.resize(ts[d].size() * 12);
        float *w = buffer.ptrw();
        for (const Transform3D &t : ts[d])
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