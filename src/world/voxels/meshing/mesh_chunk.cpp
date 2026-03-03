#include "mesh_chunk.h"
#include "voxel_terrain.h"
#include "utils.h"

using namespace godot;

const std::vector<glm::ivec3> MeshChunk::Offsets = {
    glm::ivec3(0, 0, 0), glm::ivec3(1, 0, 0), glm::ivec3(0, 1, 0), glm::ivec3(1, 1, 0),
    glm::ivec3(0, 0, 1), glm::ivec3(1, 0, 1), glm::ivec3(0, 1, 1), glm::ivec3(1, 1, 1)};

// const std::vector<Bounds> MeshChunk::RingBounds = {
//     Bounds(glm::vec3(0, -0.5f, -0.5f) + glm::vec3(-2 / leaf_count),
//            glm::vec3(0, 0.5f, 0.5f) + glm::vec3(2 / leaf_count)), // x
//     Bounds(glm::vec3(-0.5f, 0, -0.5f) + glm::vec3(-2 / leaf_count),
//            glm::vec3(0.5f, 0, 0.5f) + glm::vec3(2 / leaf_count)), // y
//     Bounds(glm::vec3(-0.5f, -0.5f, 0) + glm::vec3(-2 / leaf_count),
//            glm::vec3(0.5f, 0.5f, 0) + glm::vec3(2 / leaf_count)), // z
// };

const std::vector<glm::ivec4> MeshChunk::RingQuadChecks = {
    glm::ivec4(1, 5, 3, 7), // positive x
    glm::ivec4(0, 2, 4, 6), // negative x
    glm::ivec4(2, 3, 6, 7), // positive y
    glm::ivec4(0, 1, 4, 5), // negative y
    glm::ivec4(4, 5, 6, 7), // positive z
    glm::ivec4(0, 1, 2, 3)  // negative z
};

const std::vector<glm::vec3> MeshChunk::CheckLodOffsets = {glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0),
                                                           glm::vec3(0, 1, 0), glm::vec3(0, -1, 0),
                                                           glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)};

const std::vector<glm::ivec2> MeshChunk::Edges = {
    glm::ivec2(0, 1), glm::ivec2(2, 3), glm::ivec2(4, 5), glm::ivec2(6, 7), glm::ivec2(0, 2), glm::ivec2(1, 3),
    glm::ivec2(4, 6), glm::ivec2(5, 7), glm::ivec2(0, 4), glm::ivec2(1, 5), glm::ivec2(2, 6), glm::ivec2(3, 7)};

const std::vector<glm::ivec3> MeshChunk::YzOffsets = {glm::ivec3(0, 0, 0), glm::ivec3(0, 1, 0),
                                                      glm::ivec3(0, 0, 1), glm::ivec3(0, 1, 1)};

const std::vector<glm::ivec3> MeshChunk::XzOffsets = {glm::ivec3(0, 0, 0), glm::ivec3(1, 0, 0),
                                                      glm::ivec3(0, 0, 1), glm::ivec3(1, 0, 1)};

const std::vector<glm::ivec3> MeshChunk::XyOffsets = {glm::ivec3(0, 0, 0), glm::ivec3(1, 0, 0),
                                                      glm::ivec3(0, 1, 0), glm::ivec3(1, 1, 0)};

const std::vector<std::vector<glm::ivec3>> MeshChunk::FaceOffsets = {YzOffsets, XzOffsets, XyOffsets};

MeshChunk::MeshChunk(const JarVoxelTerrain &terrain, const VoxelOctreeNode &chunk) : leaf_count(static_cast<float>(terrain.get_chunk_size()))
{
    ChunkRes = terrain.get_chunk_size() + 2;
    LargestPos = ChunkRes - 1;
    RingBounds = {
        Bounds(glm::vec3(0, -0.5f, -0.5f) + glm::vec3(-2.0f / leaf_count),
               glm::vec3(0, 0.5f, 0.5f) + glm::vec3(2.0f / leaf_count)),
        Bounds(glm::vec3(-0.5f, 0, -0.5f) + glm::vec3(-2.0f / leaf_count),
               glm::vec3(0.5f, 0, 0.5f) + glm::vec3(2.0f / leaf_count)),
        Bounds(glm::vec3(-0.5f, -0.5f, 0) + glm::vec3(-2.0f / leaf_count),
               glm::vec3(0.5f, 0.5f, 0) + glm::vec3(2.0f / leaf_count))};

    glm::vec3 chunkCenter = chunk.get_center();
    auto cameraPosition = terrain.get_camera_position();
    // if(chunk.LoD > 0 ) return;
    Octant = glm::ivec3(chunkCenter.x > cameraPosition.x ? 1 : -1, chunkCenter.y > cameraPosition.y ? 1 : -1,
                        chunkCenter.z > cameraPosition.z ? 1 : -1);

    leaf_size = ((1 << chunk.get_lod()) * terrain.get_octree_scale());
    Bounds bounds = chunk.get_bounds(terrain.get_octree_scale()).expanded(leaf_size - 0.001f);
    nodes.clear();
    terrain.get_voxel_leaves_in_bounds(bounds, chunk.get_lod(), nodes);
    // terrain.get_voxel_leaves_in_bounds(chunk.get_bounds(terrain.get_octree_scale()).expanded( - 0.001f), chunk.get_lod(), nodes);
    innerNodeCount = nodes.size();
    bounds = bounds.expanded(0.001f);

    if (nodes.empty())
        return;

    // find if there are any lod boundaries
    const float edge_length = chunk.edge_length(terrain.get_octree_scale());
    uint16_t boundaries = chunk.compute_boundaries(terrain);
    _lodH2LBoundaries = boundaries & 0xFF;
    _lodL2HBoundaries = (boundaries >> 8) & 0xFF;

    positions.clear();
    vertexIndices.clear();
    faceDirs.clear();
    _leavesLut.clear();
    positions.resize(nodes.size(), glm::ivec3(0));
    vertexIndices.resize(nodes.size(), -2);
    faceDirs.resize(nodes.size(), 0);
    _leavesLut.resize(ChunkRes * ChunkRes * ChunkRes, 0);
    {
        float normalizingFactor = 1.0f / leaf_size;
        half_leaf_size = glm::vec3(leaf_size * 0.5);
        glm::vec3 minPos = bounds.min_corner;
        glm::ivec3 clampMax = glm::ivec3(LargestPos);

        for (size_t i = 0; i < nodes.size(); i++)
        {
            VoxelOctreeNode *node = nodes[i];
            glm::ivec3 pos = (glm::ivec3)glm::ceil((node->get_center() - minPos) * normalizingFactor) - glm::ivec3(1.0f);
            pos = glm::clamp(pos, glm::ivec3(0.0f), clampMax);

            // if (is_on_boundary(_lodH2LBoundaries, pos))
            //     continue;

            positions[i] = pos;
            vertexIndices[i] = -1;
            _leavesLut[pos.x + ChunkRes * (pos.y + ChunkRes * pos.z)] = i + 1;
        }
    }

    if (_lodH2LBoundaries != 0)
    {
        float normalizingFactor = 0.5f / leaf_size;

        // rejection_bounds = inner radius
        // acception_bounds = union of the 6 rings, should capture 8x8x2 nodes per side
        Bounds acceptance_bounds;
        Bounds rejection_bounds = chunk.get_bounds(terrain.get_octree_scale());
        for (size_t i = 0; i < CheckLodOffsets.size(); i++)
        {
            if (((_lodH2LBoundaries >> i) & 0b1) != 1)
                continue;
            glm::vec3 edge = (edge_length * 0.5f) * CheckLodOffsets[i];
            Bounds b = (RingBounds[i / 2] * edge_length) + (chunkCenter + edge);
            acceptance_bounds = acceptance_bounds.joined(b);

            glm::vec3 difference = (edge_length * 2.0f / leaf_count) * glm::abs(CheckLodOffsets[i]);
            if (i % 2 == 0)
                rejection_bounds.max_corner -= difference;
            else
                rejection_bounds.min_corner += difference;

            // UtilityFunctions::print("b: " + Utils::to_string(b.get_size() * normalizingFactor));
        }

        // UtilityFunctions::print("ab: " + Utils::to_string(acceptance_bounds) + ", rb: " +
        // Utils::to_string(rejection_bounds));
        // UtilityFunctions::print("ab: " + Utils::to_string(acceptance_bounds.get_size() * normalizingFactor) +
        //                         ", rb: " + Utils::to_string(rejection_bounds.get_size() * normalizingFactor));

        acceptance_bounds = acceptance_bounds.expanded(-0.001f);
        rejection_bounds = rejection_bounds.expanded(-0.001f);
        terrain.get_voxel_leaves_in_bounds_excluding_bounds(acceptance_bounds, rejection_bounds, chunk.get_lod() + 1,
                                                            nodes);
        ringNodeCount = nodes.size() - innerNodeCount;
        // UtilityFunctions::print(ringNodeCount);
        if (ringNodeCount <= 0)
            return;
        // should be based on full ring mode, i.e. -5 to 5 nodes
        glm::vec3 minPos = chunkCenter - 10 / leaf_count * edge_length;
        glm::ivec3 clampMax = glm::ivec3(9);
        glm::vec3 minRecPos = glm::vec3(3875439875983);
        glm::vec3 maxRecPos = glm::vec3(-3875439875983);

        for (size_t i = innerNodeCount; i < nodes.size(); i++)
        {
            VoxelOctreeNode *node = nodes[i];
            glm::ivec3 pos = (glm::ivec3)glm::ceil((node->get_center() - minPos) * normalizingFactor) - glm::ivec3(1.0f);

            minRecPos = glm::min(minRecPos, glm::vec3(pos));
            maxRecPos = glm::max(maxRecPos, glm::vec3(pos));

            pos = glm::clamp(pos, glm::ivec3(0.0f), clampMax);

            positions.push_back(pos);
            vertexIndices.push_back(-1);
            faceDirs.push_back(0);
            _ringLut[pos] = i;
        }

        // UtilityFunctions::print("min: " + Utils::to_string(minRecPos) + "max: " + Utils::to_string(maxRecPos));
    }
}

bool MeshChunk::should_have_quad(const glm::ivec3 &position, const int face) const
{
    // we might also need some cases for l2h chunks i think
    return true;
    if (_lodL2HBoundaries != 0)
        return true;
    switch (face)
    {
    case 0:
        return position.x < LargestPos;
    case 1:
        return position.y < LargestPos;
    case 2:
        return position.z < LargestPos;
    default:
        return true;
    }
}

bool MeshChunk::on_positive_edge(const glm::ivec3 &position) const
{
    // we might also need some cases for l2h chunks i think
    return (((_lodH2LBoundaries & 0b1) != 0 && position.x >= LargestPos - 2) ? 1 : 0) +
               (((_lodH2LBoundaries & 0b100) != 0 && position.y >= LargestPos - 2) ? 1 : 0) +
               (((_lodH2LBoundaries & 0b10000) != 0 && position.z >= LargestPos - 2) ? 1 : 0) >=
           2;
}

inline int MeshChunk::get_node_index_at(const glm::ivec3 &pos) const
{
    if (pos.x < 0 || pos.x >= ChunkRes || pos.y < 0 || pos.y >= ChunkRes || pos.z < 0 || pos.z >= ChunkRes)
        return -1;
    else
        return (_leavesLut[pos.x + ChunkRes * (pos.y + ChunkRes * pos.z)] - 1);
}

bool MeshChunk::get_unique_neighbouring_vertices(const glm::ivec3 &pos, const std::vector<glm::ivec3> &offsets,
                                                 std::vector<int> &result) const
{
    result.clear();
    result.reserve(offsets.size());

    for (const auto &o : offsets)
    {
        auto n = get_node_index_at(pos + o);
        if (n < 0 || vertexIndices[n] < 0)
        {
            return false;
        }
        bool duplicate = false;
        for (int existing : result)
        {
            if (existing == n)
            {
                duplicate = true;
                break;
            }
        }

        if (!duplicate)
            result.push_back(n);
    }
    return true;
}

bool MeshChunk::get_neighbours(const glm::ivec3 &pos, std::vector<int> &result) const
{
    result.resize(MeshChunk::Offsets.size());
    for (size_t i = 0; i < MeshChunk::Offsets.size(); ++i)
    {
        auto n = get_node_index_at(pos + MeshChunk::Offsets[i]);
        if (n < 0)
        {
            return false;
        }
        result[i] = n;
    }
    return true;
}

bool MeshChunk::get_ring_neighbours(const glm::ivec3 &pos, std::vector<int> &result) const
{
    result.resize(MeshChunk::Offsets.size());
    for (size_t i = 0; i < MeshChunk::Offsets.size(); ++i)
    {
        auto it = _ringLut.find(pos + MeshChunk::Offsets[i]); // Use find method
        if (it == _ringLut.end() || it->second < 0)
        {
            return false; // Value does not exist or is invalid
        }
        result[i] = it->second;
    }
    return true;
}

// make sure that the sign of the 4 vertices closest the boundary is not the same
bool MeshChunk::should_have_boundary_quad(const std::vector<int> &neighbours, const bool on_ring) const
{
    // if on ring, we check the other side. if not on ring, we check outward of the chunk
    for (size_t i = 0; i < CheckLodOffsets.size(); i++)
    {
        if (((_lodH2LBoundaries >> i) & 0b1) != 1) // this should also check where this node resides.
            continue;
        int j = i;
        if (on_ring) // swap the direction we look into
        {
            if (j % 2 == 0)
                j++;
            else
                j--;
        }

        glm::ivec4 nx = RingQuadChecks[j]; // get the right set of neighbours to consider
        int sign0 = glm::sign(nodes[neighbours[nx.x]]->get_value()),
            sign1 = glm::sign(nodes[neighbours[nx.y]]->get_value()),
            sign2 = glm::sign(nodes[neighbours[nx.z]]->get_value()),
            sign3 = glm::sign(nodes[neighbours[nx.w]]->get_value());
        if (sign0 != sign1 || sign1 != sign2 || sign2 != sign3)
            return true;
    }
    return false;
}
