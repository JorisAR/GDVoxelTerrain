#include "chunk_detail_generator.h"
#include "population_rendering.h"
#include <algorithm>
#include <cmath>

std::vector<PackedFloat32Array> ChunkDetailGenerator::generate_detail_buffers(
    const std::vector<Ref<JarTerrainDetail>> &details, const std::vector<int> &detailBiomes,
    const Ref<JarBiomeMap> &biomeMap, const std::shared_ptr<const GroundPalette> &ground,
    const DetailInputs &inputs) const
{
    if (details.empty() || !inputs.valid())
        return {};
    const Vector3 *verts = inputs.vertices.ptr();
    const Vector3 *normals = inputs.normals.ptr();
    const int32_t *indices = inputs.indices.ptr();
    const int64_t indexCount = inputs.indices.size();
    const int64_t paintedCount = inputs.painted.size();
    const float *paintedAmount = inputs.painted.ptr();
    const glm::vec3 chunkCenter = inputs.center;
    const Vector3 chunkOffset(chunkCenter.x, chunkCenter.y, chunkCenter.z);
    const bool biomes = biomeMap.is_valid();

    // Painted weight of a vertex: details only grow on unpainted ground.
    auto painted = [&](int v) { return v < paintedCount ? paintedAmount[v] : 0.0f; };

    // Copied out of the resources once: the loop below runs for every triangle.
    // Candidates come from a lattice in volume space, one jittered point per
    // cell of `spacing`, on the plane facing the triangle most (x, y or z): a
    // point's place and random values depend on that cell only, so the same
    // ground gets the same details whatever the voxel size, mesher or LOD made
    // it (only their height follows the mesh). A triangle projects onto that
    // plane by at least 1/sqrt(3) of its area: the lattice is that much denser
    // and thinned by the actual projection, giving `density` per square meter
    // of ground on any slope.
    constexpr float MinProjection = 0.57735f; // 1 / sqrt(3)
    struct Active
    {
        const JarTerrainDetail *detail;
        int index; // into `details`
        int biome;
        float spacing; // lattice cell size (m)
    };
    std::vector<Active> active;
    for (int d = 0; d < int(details.size()); d++)
    {
        const JarTerrainDetail *detail = details[d].ptr();
        if (detail != nullptr && detail->get_density() > 0.0f)
            active.push_back({detail, d, d < int(detailBiomes.size()) ? detailBiomes[d] : -1,
                              std::sqrt(MinProjection / detail->get_density())});
    }

    std::vector<std::vector<Transform3D>> transforms(details.size());
    std::vector<std::vector<glm::vec3>> colors(details.size());
    for (int64_t i = 0; i + 2 < indexCount; i += 3)
    {
        const int ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
        // Volume space: the lattice is global.
        const Vector3 a = verts[ia] + chunkOffset, b = verts[ib] + chunkOffset, c = verts[ic] + chunkOffset;
        const Vector3 cross = (b - a).cross(c - a);
        const float crossLength = cross.length();
        if (crossLength < 1e-8f)
            continue;
        // The lattice plane: across the axis the triangle faces most.
        const Vector3 facing = cross.abs() / crossLength;
        const int axis = facing.x >= facing.y && facing.x >= facing.z ? 0 : (facing.y >= facing.z ? 1 : 2);
        const int ua = (axis + 1) % 3, va = (axis + 2) % 3;
        const float keepProjection = MinProjection / facing[axis]; // <= 1
        // Projected triangle and its barycentric setup.
        const float ax = a[ua], ay = a[va], bx = b[ua], by = b[va], cx = c[ua], cy = c[va];
        const float det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (std::abs(det) < 1e-10f)
            continue;
        const float minU = std::min({ax, bx, cx}), maxU = std::max({ax, bx, cx});
        const float minV = std::min({ay, by, cy}), maxV = std::max({ay, by, cy});
        const Vector3 centroid = (a + b + c) / 3.0f;
        const glm::vec3 centroidVolume(centroid.x, centroid.y, centroid.z);
        // Biome weights and the ground color vary over many meters: once per
        // triangle (about a square meter), for all its instances and details,
        // instead of per instance (several noise lookups each).
        bool weightsReady = false, colorReady = false;
        BiomeWeights weights;
        glm::vec3 color(0.3f, 0.35f, 0.2f);
        auto biome_weights = [&]() -> const BiomeWeights & {
            if (!weightsReady)
            {
                weights = biomeMap->evaluate(centroidVolume, _upField);
                weightsReady = true;
            }
            return weights;
        };
        for (const Active &entry : active)
        {
            float biomeWeight = 1.0f;
            if (entry.biome >= 0 && biomes)
            {
                biomeWeight = biome_weights().weight_of(entry.biome);
                if (biomeWeight <= 0.0f)
                    continue; // not this detail's biome: skip before any per-instance work
            }
            const float g = entry.spacing;
            const int u0 = int(std::floor(minU / g)), u1 = int(std::floor(maxU / g));
            const int v0 = int(std::floor(minV / g)), v1 = int(std::floor(maxV / g));
            for (int cu = u0; cu <= u1; cu++)
                for (int cv = v0; cv <= v1; cv++)
                {
                    PopulationRandom rng(PopulationRandom::hash(uint32_t(cu), uint32_t(cv),
                                                                uint32_t(axis) | (uint32_t(entry.index) << 2), 0xde7a11u));
                    const float pu = (float(cu) + rng.next()) * g, pv = (float(cv) + rng.next()) * g;
                    const float w0 = ((by - cy) * (pu - cx) + (cx - bx) * (pv - cy)) / det;
                    const float w1 = ((cy - ay) * (pu - cx) + (ax - cx) * (pv - cy)) / det;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                        continue; // another triangle's point
                    const float rotation = rng.next(), scale = rng.next(), keep = rng.next();
                    if (rng.next() >= keepProjection)
                        continue;
                    if (keep >= biomeWeight)
                        continue;
                    if (w0 * painted(ia) + w1 * painted(ib) + w2 * painted(ic) > 0.5f)
                        continue;
                    const Vector3 p = a * w0 + b * w1 + c * w2;
                    const glm::vec3 position(p.x, p.y, p.z);
                    const Vector3 n = (normals[ia] * w0 + normals[ib] * w1 + normals[ic] * w2).normalized();
                    const glm::vec3 normal(n.x, n.y, n.z);
                    const glm::vec3 up = _upField.up(position);
                    // `keep` already passed the biome weight; scale it for the rest.
                    if (keep >= biomeWeight * entry.detail->probability(position, _upField.height(position), normal, up))
                        continue;
                    // Chunk-relative: the MultiMesh lives under the chunk node.
                    Transform3D t = entry.detail->build_transform(position, up, normal, rotation, scale);
                    t.origin -= chunkOffset;
                    transforms[entry.index].push_back(t);
                    if (!colorReady)
                    {
                        if (ground)
                            color = biomes ? ground->color_for(biome_weights()) : ground->color_at(position, _upField);
                        colorReady = true;
                    }
                    colors[entry.index].push_back(color);
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
