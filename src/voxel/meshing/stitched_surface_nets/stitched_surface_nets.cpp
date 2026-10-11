#include "stitched_surface_nets.h"
#include <unordered_set>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "voxel_volume.h"
#include "utility/utils.h"
#include "qef.h"
#include <limits>

StitchedSurfaceNets::StitchedSurfaceNets(const JarVoxelVolume &terrain, const VoxelOctreeNode &chunk)
    : _chunk(&chunk), _cubicVoxels(terrain.get_cubic_voxels()),
      _dualContouring(terrain.get_meshing_algorithm() == JarVoxelVolume::MESHING_DUAL_CONTOURING),
      _manifold(terrain.get_manifold() && !terrain.get_cubic_voxels()),
      _featureThreshold(terrain.get_dual_contouring_feature_threshold()), _sdf(terrain.get_sdf_ptr()),
      _meshChunk(StitchedMeshChunk(terrain, chunk))
{
    if (_manifold)
    {
        std::array<uint8_t, 12> none;
        none.fill(NoComponent);
        _edgeComponent.resize(_meshChunk.nodes.size(), none);
    }
    if (_dualContouring)
    {
        _gradients.resize(_meshChunk.nodes.size());
        _distanceScale.resize(_meshChunk.nodes.size(), 1.0f);
        _hasGradient.resize(_meshChunk.nodes.size(), false);
    }
}

glm::vec3 StitchedSurfaceNets::gradient_at(int node_id)
{
    if (_hasGradient[node_id])
        return _gradients[node_id];
    const VoxelOctreeNode *node = _meshChunk.nodes[node_id];
    if (node->has_normal())
        _gradients[node_id] = node->get_normal(); // edits store exact normals of exact distances
    else
    {
        // The volume SDF may not be an exact distance (e.g. height minus noise
        // displacement): divide by the gradient length so the value is a true
        // distance locally. Otherwise sample planes land at the wrong height and
        // slopes come out terraced.
        const glm::vec3 g = _sdf->gradient(node->_center);
        const float len = glm::length(g);
        _gradients[node_id] = len > 0.0f ? g / len : glm::vec3(0.0f, 1.0f, 0.0f);
        _distanceScale[node_id] = 1.0f / std::clamp(len, 0.2f, 5.0f);
    }
    _hasGradient[node_id] = true;
    return _gradients[node_id];
}

glm::vec3 StitchedSurfaceNets::place_dual_contouring_vertex(const int node_id, const std::vector<int> &neighbours,
                                                             glm::vec3 &averageNormal, uint16_t edgeMask)
{
    // Corners touching the given edges (all corners when every edge is used).
    uint8_t cornerMask = edgeMask == 0x0FFF ? 0xFF : 0;
    if (cornerMask == 0)
        for (int e = 0; e < 12; ++e)
            if ((edgeMask >> e) & 1)
                cornerMask |= uint8_t((1 << StitchedMeshChunk::Edges[e].x) | (1 << StitchedMeshChunk::Edges[e].y));

    // Every corner sample near the surface is a distance d along normal n, so
    // the surface passes through (center - d n) with normal n: one tangent
    // plane per corner. Their least-squares intersection recovers edges and
    // corners that lie between the samples.
    Qef qef;
    glm::vec3 cellMin(std::numeric_limits<float>::max()), cellMax(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; ++i)
    {
        const VoxelOctreeNode *n = _meshChunk.nodes[neighbours[i]];
        cellMin = glm::min(cellMin, n->_center);
        cellMax = glm::max(cellMax, n->_center);
    }
    const float cellSize = cellMax.x - cellMin.x;
    _lastVertexSharp = false;
    glm::vec3 firstNormal(0.0f);
    float minAgreement = 1.0f; // lowest cosine between the samples' normals
    for (int i = 0; i < 8; ++i)
    {
        if (((cornerMask >> i) & 1) == 0)
            continue;
        VoxelOctreeNode *n = _meshChunk.nodes[neighbours[i]];
        const glm::vec3 normal = gradient_at(neighbours[i]);
        const float d = n->get_value() * _distanceScale[neighbours[i]];
        if (std::abs(d) > 1.5f * cellSize)
            continue; // too far for its closest feature to be in this cell
        if (qef.count() == 0)
            firstNormal = normal;
        minAgreement = std::min(minAgreement, glm::dot(firstNormal, normal));
        qef.add(n->_center - d * normal, normal);
        averageNormal += normal;
    }
    const float normalLength = glm::length(averageNormal);
    averageNormal = normalLength > 1e-6f ? averageNormal / normalLength : glm::vec3(0.0f);
    if (qef.count() == 0)
        return glm::vec3(std::numeric_limits<float>::quiet_NaN());
    // Where the surface crosses the cell's edges: always inside the cell, the
    // target the solution is pulled towards along unconstrained directions.
    glm::vec3 crossings(0.0f);
    int crossingCount = 0;
    for (int e = 0; e < 12; ++e)
    {
        if (((edgeMask >> e) & 1) == 0)
            continue;
        const auto &edge = StitchedMeshChunk::Edges[e];
        const VoxelOctreeNode *na = _meshChunk.nodes[neighbours[edge.x]];
        const VoxelOctreeNode *nb = _meshChunk.nodes[neighbours[edge.y]];
        const float va = const_cast<VoxelOctreeNode *>(na)->get_value();
        const float vb = const_cast<VoxelOctreeNode *>(nb)->get_value();
        if (glm::sign(va) == glm::sign(vb))
            continue;
        crossings += glm::mix(na->_center, nb->_center, std::abs(va) / (std::abs(va) + std::abs(vb)));
        crossingCount++;
    }
    const glm::vec3 target = crossingCount > 0 ? crossings / float(crossingCount) : qef.mass_point();

    // Try the full feature first (corner), then simpler ones (edge, plane):
    // a corner or edge outside the cell belongs to a neighbouring cell, and
    // this cell's vertex then lies on the remaining edge or face.
    const glm::vec3 tolerance(cellSize * 0.01f);
    for (int rank = qef.rank(_featureThreshold); rank >= 1; --rank)
    {
        Qef::Solution solution = qef.solve_rank(rank, _featureThreshold);
        if (Qef::fit_into(solution, target, cellMin - tolerance, cellMax + tolerance))
        {
            // An edge or corner solution where the normals really disagree is a
            // sharp feature (not just curvature).
            constexpr float SharpAgreement = 0.8f; // ~37 degrees
            _lastVertexSharp = rank >= 2 && minAgreement < SharpAgreement;
            return glm::clamp(solution.point, cellMin, cellMax);
        }
    }
    return glm::vec3(std::numeric_limits<float>::quiet_NaN());
}

void StitchedSurfaceNets::create_vertex(const int node_id, const std::vector<int> &neighbours, const bool on_ring)
{
    glm::vec3 vertexPosition(0.0f);
    glm::vec4 color(0, 0, 0, 0);
    glm::vec3 normal(0.0f);
    int duplicates = 0, edge_crossings = 0;
    for (auto &edge : StitchedMeshChunk::Edges)
    {
        auto ai = neighbours[edge.x];
        auto bi = neighbours[edge.y];
        auto na = _meshChunk.nodes[ai];
        auto nb = _meshChunk.nodes[bi];
        float valueA = na->get_value();
        float valueB = nb->get_value();
        glm::vec3 posA = na->_center;
        glm::vec3 posB = nb->_center;

        normal += (valueB - valueA) * (posB - posA);

        if (glm::sign(valueA) == glm::sign(valueB))
            continue;

        float t = glm::abs(valueA) / (glm::abs(valueA) + glm::abs(valueB));
        vertexPosition += glm::mix(posA, posB, t);
        edge_crossings++;
        color += glm::mix(na->get_materials().low(), nb->get_materials().low(), t);
    }

    if (edge_crossings <= 0)
        return;

    //computes and stores the directions in which to generate quads, also determines winding order
    _meshChunk.faceDirs[node_id] =
        (static_cast<int>(glm::sign(glm::sign(_meshChunk.nodes[neighbours[6]]->get_value()) -
                                    glm::sign(_meshChunk.nodes[neighbours[7]]->get_value())) +
                          1))
            << 0 |
        (static_cast<int>(glm::sign(glm::sign(_meshChunk.nodes[neighbours[7]]->get_value()) -
                                    glm::sign(_meshChunk.nodes[neighbours[5]]->get_value())) +
                          1))
            << 2 |
        (static_cast<int>(glm::sign(glm::sign(_meshChunk.nodes[neighbours[3]]->get_value()) -
                                    glm::sign(_meshChunk.nodes[neighbours[7]]->get_value())) +
                          1))
            << 4;

    if (_manifold)
    {
        create_component_vertices(node_id, neighbours);
        register_edge_node(node_id, neighbours, on_ring);
        return;
    }

    vertexPosition /= static_cast<float>(edge_crossings);
    color /= static_cast<float>(edge_crossings);
    normal = glm::normalize(normal);
    if (_cubicVoxels)
        vertexPosition = _meshChunk.nodes[node_id]->_center;
    else if (_dualContouring)
    {
        // Ring (LOD seam) vertices are copies of the neighbouring coarser
        // chunk's vertices; they must be placed exactly as that chunk places
        // them (a vertex depends only on its cell's corner samples), or the
        // seam cracks.
        glm::vec3 exactNormal(0.0f);
        const glm::vec3 dc = place_dual_contouring_vertex(node_id, neighbours, exactNormal);
        if (!glm::any(glm::isnan(dc)))
            vertexPosition = dc;
        if (glm::dot(exactNormal, exactNormal) > 0.5f)
            normal = exactNormal; // exact surface normal instead of the value-difference estimate
    }

    vertexPosition -= _chunk->_center;
    int vertexIndex = _verts.size();
    register_edge_node(node_id, neighbours, on_ring);

    _meshChunk.vertexIndices[node_id] = vertexIndex;
    _sharp.push_back(_dualContouring && _lastVertexSharp ? 1 : 0);
    _verts.push_back({vertexPosition.x, vertexPosition.y, vertexPosition.z});
    _normals.push_back({normal.x, normal.y, normal.z});
    _colors.push_back({color.r, color.g, color.b, color.a});
}

ChunkMeshData *StitchedSurfaceNets::generate_mesh_data(const JarVoxelVolume &terrain)
{
    // One scratch buffer for all neighbour queries: allocating a vector per
    // voxel was a large share of meshing time.
    std::vector<int> neighbours;
    neighbours.reserve(8);
    for (size_t node_id = 0; node_id < _meshChunk.innerNodeCount; node_id++)
    {
        if (_meshChunk.vertexIndices[node_id] <= -2)
            continue;
        neighbours.clear();
        glm::ivec3 grid_position = _meshChunk.positions[node_id];

        if (!_meshChunk.get_neighbours(grid_position, neighbours))
            continue;
        create_vertex(node_id, neighbours, false);
    }

    if (_verts.size() == 0)    
        return nullptr;    

    // if on lod boundary, add an additional pass
    for (size_t node_id = _meshChunk.innerNodeCount; node_id < _meshChunk.innerNodeCount + _meshChunk.ringNodeCount;
         node_id++)
    {
        if (_meshChunk.vertexIndices[node_id] <= -2)
            continue;
        neighbours.clear();
        glm::ivec3 grid_position = _meshChunk.positions[node_id];

        if (!_meshChunk.get_ring_neighbours(grid_position, neighbours))
            continue;
        create_vertex(node_id, neighbours, true);
        // if (false)
        // { // print the nodes itself
        //     glm::vec3 vertexPosition = _meshChunk.nodes[node_id]->_center;
        //     vertexPosition -= _chunk->_center;
        //     int vertexIndex = (_verts.size());
        //     _meshChunk.vertexIndices[node_id] = vertexIndex;
        //     _verts.push_back({vertexPosition.x, vertexPosition.y, vertexPosition.z});
        //     _normals.push_back({0, 1, 0});
        //     _colors.push_back(Color(0, 0, 0, 0));
        //     _edgeIndices[grid_position] = (vertexIndex);
        // }
    }
    // godot::String ringNodes = "";
    // for (auto& [position, vertexId]: _ringEdgeNodes)
    // {
    //     ringNodes += Utils::to_string(position) + ", ";
    // }
    // godot::String innerNodes = "";
    // for (auto& [position, vertexId]: _innerEdgeNodes)
    // {
    //     innerNodes += Utils::to_string(position) + ", ";
    // }
    // UtilityFunctions::print("Ring Nodes: " + ringNodes);
    // UtilityFunctions::print("Inner Nodes: " + innerNodes);

    for (size_t node_id = 0; node_id < _meshChunk.innerNodeCount; node_id++)
    {
        if (_meshChunk.vertexIndices[node_id] <= -1)
            continue;

        auto pos = _meshChunk.positions[node_id];
        auto faceDirs = _meshChunk.faceDirs[node_id];
        static const int faces = 3;
        for (int i = 0; i < faces; i++)
        {
            int flipFace = ((faceDirs >> (2 * i)) & 3) - 1;
            if (flipFace == 0 || !_meshChunk.should_have_quad(pos, i))
                continue;

            neighbours.clear();
            if (_meshChunk.get_unique_neighbouring_vertices(pos, StitchedMeshChunk::FaceOffsets[i], neighbours) &&
                neighbours.size() == 4)
            {
                int n[4];
                bool ok = true;
                for (int k = 0; k < 4; ++k)
                {
                    n[k] = _meshChunk.vertexIndices[neighbours[k]];
                    if (_manifold)
                    {
                        // The quad's edge, seen from cell k, and the piece it is part of there.
                        const uint8_t component = _edgeComponent[neighbours[k]][quad_edge(i, k)];
                        ok = ok && component != NoComponent;
                        n[k] += component;
                    }
                }
                if (ok)
                    add_quad(n[0], n[1], n[2], n[3], flipFace == -1);
            }
        }
    }

    if (_indices.size() == 0)    
        return nullptr;
    

    if (_meshChunk.is_edge_chunk())
    {
        // go through inner node edges, then if some inner node edge in +x/y/z exists, attempt to find a ring vertices
        // around this. make triangle/quad depending on what you find
        // The search below finds most stitch triangles several times (from each
        // inner edge node around it): emit each vertex triple only once.
        std::unordered_set<uint64_t> stitched;
        auto stitch = [&](int a, int b, int c) {
            if (a == b || b == c || a == c)
                return;
            int k[3] = {a, b, c};
            std::sort(k, k + 3);
            const uint64_t key = (uint64_t(k[0]) << 42) | (uint64_t(k[1]) << 21) | uint64_t(k[2]);
            if (stitched.insert(key).second)
                add_tri_fix_normal(a, b, c);
        };
        for (auto &[pos, node_id] : _innerEdgeNodes)
        {
            static const int faces = 3;
            static const glm::ivec3 offsets[3] = {glm::ivec3(1, 0, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, 1)};
            auto faceDirs = _meshChunk.faceDirs[node_id];
            for (int i = 0; i < faces; i++)
            {
                int innerNeighbour = node_id;
                if(!(_meshChunk.on_positive_edge(pos))) {
                    auto it = _innerEdgeNodes.find(pos + offsets[i]);
                    if (it == _innerEdgeNodes.end() || it->second < 0)
                        continue;
                    innerNeighbour = it->second;
                }

                std::vector<std::vector<int>> neighboursLists = find_ring_nodes(pos, i);
                int n0 = _meshChunk.vertexIndices[node_id];
                int n1 = _meshChunk.vertexIndices[innerNeighbour];
                for (auto &neighbours : neighboursLists)
                {
                    if (neighbours.size() == 2)
                    {
                        int n2 = _meshChunk.vertexIndices[neighbours[0]];
                        int n3 = _meshChunk.vertexIndices[neighbours[1]];
                        if (_verts[n0].distance_squared_to(_verts[n3]) < _verts[n1].distance_squared_to(_verts[n2]))
                        {
                            stitch(n0, n1, n3);
                            stitch(n0, n3, n2);
                        }
                        else
                        {
                            stitch(n1, n3, n2);
                            stitch(n1, n2, n0);
                        }
                    }
                    else if (neighbours.size() == 1)
                    {
                        int n2 = _meshChunk.vertexIndices[neighbours[0]];
                        stitch(n0, n1, n2);
                    }
                }
            }
        }
    }

    if (_dualContouring && !_cubicVoxels)
        split_creases();

    Array meshData;
    meshData.resize(Mesh::ARRAY_MAX);
    meshData[Mesh::ARRAY_VERTEX] = _verts;
    meshData[Mesh::ARRAY_NORMAL] = _normals;
    meshData[Mesh::ARRAY_COLOR] = _colors;
    meshData[Mesh::ARRAY_INDEX] = _indices;

    ChunkMeshData *output =
        new ChunkMeshData(meshData, _chunk->get_lod(), _meshChunk.is_edge_chunk(), _chunk->get_bounds(terrain.get_voxel_size()));
    output->boundaries = _meshChunk._lodH2LBoundaries | (_meshChunk._lodL2HBoundaries << 8);
    output->mesh_version = terrain.get_mesh_version();

    return output;
}

//this function finds way too many possible nodes.
//Possible improvement: check the octant, e.g. we dont need to find ringnodes towards neighbours of the same LOD
//Possible improvement: use the same system of a crossed edge as before to verify if we need a quad or not
std::vector<std::vector<int>> StitchedSurfaceNets::find_ring_nodes(const glm::ivec3 &pos, const int face) const
{
    static const glm::ivec3 face_offsets[3] = {glm::ivec3(1, 0, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, 1)};
    static const glm::ivec3 ring_offsets[3][8] = {
        {glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1), glm::ivec3(0, 1, 1), glm::ivec3(0, -1, -1), glm::ivec3(0, -1, 1), glm::ivec3(0, 1, -1)},
        {glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0), glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1), glm::ivec3(1, 0, 1), glm::ivec3(-1, 0, -1), glm::ivec3(-1, 0, 1), glm::ivec3(1, 0, -1)},
        {glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0), glm::ivec3(1, 1, 0), glm::ivec3(-1, -1, 0), glm::ivec3(-1, 1, 0), glm::ivec3(1, -1, 0)}};

        // static const glm::ivec3 ring_offsets[3][4] = {
        //     {glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1)},
        //     {glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0), glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1)},
        //     {glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0)}};
    // function to go from inner coordinates to ring coordinates
    auto get_ring_node = [this](const glm::ivec3 pos) {
        glm::ivec3 ring_pos = glm::floor((glm::vec3(pos)) / 2.0f);

        auto it = _ringEdgeNodes.find(ring_pos);
        if (it == _ringEdgeNodes.end() || it->second < 0 || _meshChunk.vertexIndices[it->second] < 0)
            return -1;

        return it->second;
    };

    std::vector<std::vector<int>> result;
    for (size_t i = 0; i < 3; i++)//check all directions?
    {
        for (auto dir : ring_offsets[i])
        {
            int n0 = get_ring_node(pos + dir);
            int n1 = get_ring_node(pos + dir + face_offsets[i]);
    
            if (n0 >= 0 && n1 >= 0)
                result.push_back({n0, n1});
            else if (n0 >= 0)
                result.push_back({n0});
            else if (n1 >= 0)
                result.push_back({n1});
        }
    }
    

    return result;
}

//I'd rather not base the winding order on the normal, but it works for now. Only required for the edge chunk.
namespace
{
// Cube corners are numbered x + 2y + 4z; StitchedMeshChunk::Edges lists the 12
// edges (0-3 along x, 4-7 along y, 8-11 along z). Each face: its 4 corners in
// loop order and the edges between consecutive corners (edge k joins corner k
// and corner k + 1).
struct CubeFace
{
    int corners[4];
    int edges[4];
};
constexpr CubeFace CubeFaces[6] = {
    {{0, 2, 6, 4}, {4, 10, 6, 8}}, // x = 0
    {{1, 3, 7, 5}, {5, 11, 7, 9}}, // x = 1
    {{0, 1, 5, 4}, {0, 9, 2, 8}},  // y = 0
    {{2, 3, 7, 6}, {1, 11, 3, 10}}, // y = 1
    {{0, 1, 3, 2}, {0, 5, 1, 4}},  // z = 0
    {{4, 5, 7, 6}, {2, 7, 3, 6}},  // z = 1
};

int find_root(int parent[12], int e)
{
    while (parent[e] != e)
        e = parent[e] = parent[parent[e]];
    return e;
}
} // namespace

// Edge index (in StitchedMeshChunk::Edges) of the grid edge shared by the 4
// cells of a quad, seen from cell k of FaceOffsets[face].
int StitchedSurfaceNets::quad_edge(int face, int k)
{
    // Built once; function-local static initialization is thread-safe.
    struct Table
    {
        int edge[3][4];
    };
    static const Table table = [] {
        Table t{};
        for (int f = 0; f < 3; ++f)
            for (int c = 0; c < 4; ++c)
            {
                glm::ivec3 axis(0);
                axis[f] = 1;
                const glm::ivec3 a = glm::ivec3(1) - axis - StitchedMeshChunk::FaceOffsets[f][c];
                const glm::ivec3 b = a + axis;
                const int ca = a.x + 2 * a.y + 4 * a.z, cb = b.x + 2 * b.y + 4 * b.z;
                for (int e = 0; e < 12; ++e)
                    if (StitchedMeshChunk::Edges[e].x == ca && StitchedMeshChunk::Edges[e].y == cb)
                        t.edge[f][c] = e;
            }
        return t;
    }();
    return table.edge[face][k];
}

// Pieces of surface inside a cell: crossed edges are connected along the
// cube's faces. A face with 4 crossings is ambiguous; the average of its 4
// corner values decides which corners connect through the face center. Both
// cells sharing a face see the same values, so they always agree, which is
// what keeps the result manifold.
int StitchedSurfaceNets::find_components(const std::vector<int> &neighbours,
                                         std::array<uint8_t, 12> &edgeComponent) const
{
    float value[8];
    bool inside[8];
    for (int i = 0; i < 8; ++i)
    {
        value[i] = _meshChunk.nodes[neighbours[i]]->get_value();
        inside[i] = value[i] < 0.0f;
    }
    bool crossed[12];
    int parent[12];
    for (int e = 0; e < 12; ++e)
    {
        crossed[e] = inside[StitchedMeshChunk::Edges[e].x] != inside[StitchedMeshChunk::Edges[e].y];
        parent[e] = e;
    }
    auto unite = [&](int a, int b) { parent[find_root(parent, a)] = find_root(parent, b); };
    for (const CubeFace &face : CubeFaces)
    {
        int count = 0;
        for (int k = 0; k < 4; ++k)
            count += crossed[face.edges[k]] ? 1 : 0;
        if (count == 2)
        {
            int first = -1;
            for (int k = 0; k < 4; ++k)
                if (crossed[face.edges[k]])
                {
                    if (first < 0)
                        first = face.edges[k];
                    else
                        unite(first, face.edges[k]);
                }
        }
        else if (count == 4)
        {
            // Corners 0 and 2 share a side. If the face center is on their side,
            // they connect through it, and the crossings around corners 1 and 3
            // pair up; otherwise those around corners 0 and 2 do.
            float center = 0.0f;
            for (int k = 0; k < 4; ++k)
                center += value[face.corners[k]];
            const bool centerInside = center < 0.0f;
            if (centerInside == inside[face.corners[0]])
            {
                unite(face.edges[0], face.edges[1]); // around corner 1
                unite(face.edges[2], face.edges[3]); // around corner 3
            }
            else
            {
                unite(face.edges[3], face.edges[0]); // around corner 0
                unite(face.edges[1], face.edges[2]); // around corner 2
            }
        }
    }
    int components = 0;
    int rootComponent[12];
    std::fill(rootComponent, rootComponent + 12, -1);
    for (int e = 0; e < 12; ++e)
    {
        if (!crossed[e])
        {
            edgeComponent[e] = NoComponent;
            continue;
        }
        const int root = find_root(parent, e);
        if (rootComponent[root] < 0)
            rootComponent[root] = components++;
        edgeComponent[e] = uint8_t(rootComponent[root]);
    }
    return components;
}

// One vertex per surface piece of the cell, placed by the active mesher from
// that piece's edges only.
void StitchedSurfaceNets::create_component_vertices(const int node_id, const std::vector<int> &neighbours)
{
    std::array<uint8_t, 12> &edgeComponent = _edgeComponent[node_id];
    const int components = find_components(neighbours, edgeComponent);
    _meshChunk.vertexIndices[node_id] = _verts.size();
    for (int c = 0; c < components; ++c)
    {
        uint16_t edgeMask = 0;
        glm::vec3 position(0.0f), normal(0.0f);
        glm::vec4 color(0.0f);
        int crossings = 0;
        for (int e = 0; e < 12; ++e)
        {
            if (edgeComponent[e] != c)
                continue;
            edgeMask |= uint16_t(1 << e);
            VoxelOctreeNode *na = _meshChunk.nodes[neighbours[StitchedMeshChunk::Edges[e].x]];
            VoxelOctreeNode *nb = _meshChunk.nodes[neighbours[StitchedMeshChunk::Edges[e].y]];
            const float va = na->get_value(), vb = nb->get_value();
            const float t = std::abs(va) / (std::abs(va) + std::abs(vb));
            position += glm::mix(na->_center, nb->_center, t);
            color += glm::mix(na->get_materials().low(), nb->get_materials().low(), t);
            normal += (vb - va) * (nb->_center - na->_center);
            crossings++;
        }
        position /= float(crossings);
        color /= float(crossings);
        normal = glm::length(normal) > 0.0f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
        if (_dualContouring)
        {
            glm::vec3 exactNormal(0.0f);
            const glm::vec3 dc = place_dual_contouring_vertex(node_id, neighbours, exactNormal, edgeMask);
            if (!glm::any(glm::isnan(dc)))
                position = dc;
            if (glm::dot(exactNormal, exactNormal) > 0.5f)
                normal = exactNormal;
        }
        position -= _chunk->_center;
        _sharp.push_back(_dualContouring && _lastVertexSharp ? 1 : 0);
        _verts.push_back({position.x, position.y, position.z});
        _normals.push_back({normal.x, normal.y, normal.z});
        _colors.push_back({color.r, color.g, color.b, color.a});
    }
}

void StitchedSurfaceNets::register_edge_node(const int node_id, const std::vector<int> &neighbours, const bool on_ring)
{
    const glm::ivec3 grid_position = _meshChunk.positions[node_id];
    if ((on_ring || _meshChunk.is_on_any_boundary(grid_position)) &&
        _meshChunk.should_have_boundary_quad(neighbours, on_ring))
    {
        if (on_ring)
            _ringEdgeNodes[grid_position] = node_id;
        else
            _innerEdgeNodes[grid_position] = node_id;
    }
}

// Quad n0 n1 / n2 n3 (n0-n3 and n1-n2 are the diagonals).
void StitchedSurfaceNets::add_quad(int n0, int n1, int n2, int n3, bool flip)
{
    bool splitAlong03 = _verts[n0].distance_squared_to(_verts[n3]) < _verts[n1].distance_squared_to(_verts[n2]);
    if (_dualContouring)
    {
        // Pick the diagonal whose triangles agree best with their vertices'
        // surface normals: across a sharp edge that is the diagonal along it.
        auto agreement = [this](int a, int b, int c) {
            const Vector3 n = (_verts[b] - _verts[a]).cross(_verts[c] - _verts[a]);
            const real_t len = n.length();
            if (len <= 1e-12f)
                return real_t(0);
            const Vector3 u = n / len;
            return std::min(std::abs(u.dot(_normals[a])), std::min(std::abs(u.dot(_normals[b])), std::abs(u.dot(_normals[c]))));
        };
        const real_t along03 = std::min(agreement(n0, n1, n3), agreement(n0, n3, n2));
        const real_t along12 = std::min(agreement(n1, n3, n2), agreement(n1, n2, n0));
        splitAlong03 = along03 >= along12;
    }
    if (splitAlong03)
    {
        add_tri(n0, n1, n3, flip);
        add_tri(n0, n3, n2, flip);
    }
    else
    {
        add_tri(n1, n3, n2, flip);
        add_tri(n1, n2, n0, flip);
    }
}

// Smooth normals within a surface, but not across sharp creases: each triangle
// corner averages only the neighbouring faces within CreaseAngle of its own
// face, and vertices whose corners disagree are duplicated.
void StitchedSurfaceNets::split_creases()
{
    constexpr float SharpCreaseCos = 0.82f;  // ~35 degrees
    constexpr float SmoothCreaseCos = 0.5f;  // 60 degrees
    const int64_t triCount = _indices.size() / 3;
    const int64_t vertCount = _verts.size();
    std::vector<Vector3> faceNormals(triCount);
    std::vector<std::vector<int>> vertexTris(vertCount);
    for (int64_t t = 0; t < triCount; ++t)
    {
        const int a = _indices[3 * t], b = _indices[3 * t + 1], c = _indices[3 * t + 2];
        // Area weighted, outward: Godot's front faces are wound clockwise.
        faceNormals[t] = (_verts[c] - _verts[a]).cross(_verts[b] - _verts[a]);
        vertexTris[a].push_back(int(t));
        vertexTris[b].push_back(int(t));
        vertexTris[c].push_back(int(t));
    }
    int32_t *indices = _indices.ptrw();
    for (int64_t v = 0; v < vertCount; ++v)
    {
        const std::vector<int> &tris = vertexTris[v];
        if (tris.empty())
            continue;
        // Vertices on sharp features split at a tight angle; elsewhere only
        // strong creases split, so coarse curved surfaces stay smooth.
        const float creaseCos = _sharp[v] ? SharpCreaseCos : SmoothCreaseCos;
        // Normals already assigned to this vertex (first) and its copies.
        std::vector<std::pair<Vector3, int>> variants;
        for (int t : tris)
        {
            const Vector3 own = faceNormals[t].normalized();
            Vector3 sum;
            for (int o : tris)
                if (faceNormals[o].normalized().dot(own) >= creaseCos)
                    sum += faceNormals[o];
            const Vector3 n = sum.length_squared() > 0.0f ? sum.normalized() : own;

            int target = -1;
            for (const auto &[normal, index] : variants)
                if (normal.dot(n) > 0.999f)
                {
                    target = index;
                    break;
                }
            if (target < 0)
            {
                if (variants.empty())
                    target = int(v);
                else
                {
                    target = int(_verts.size());
                    _verts.push_back(_verts[v]);
                    _colors.push_back(_colors[v]);
                    _normals.push_back(n);
                }
                variants.emplace_back(n, target);
                if (target == v)
                    _normals.set(v, n);
            }
            for (int k = 0; k < 3; ++k)
                if (indices[3 * t + k] == v)
                    indices[3 * t + k] = target;
        }
    }
}

void StitchedSurfaceNets::add_tri_fix_normal(int n0, int n1, int n2)
{
    godot::Vector3 normal = (_verts[n1] - _verts[n0]).cross(_verts[n2] - _verts[n0]);
    add_tri(n0, n1, n2, normal.dot(_normals[n0]) > 0);
}

void StitchedSurfaceNets::add_tri(int n0, int n1, int n2, bool flip)
{
    if (!flip)
    {
        _indices.push_back(n0);
        _indices.push_back(n1);
        _indices.push_back(n2);
    }
    else
    {
        _indices.push_back(n1);
        _indices.push_back(n0);
        _indices.push_back(n2);
    }
}