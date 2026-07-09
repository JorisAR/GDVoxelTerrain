#include "stitched_surface_nets.h"
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "voxel_terrain.h"
#include "utility/utils.h"

StitchedSurfaceNets::StitchedSurfaceNets(const JarVoxelTerrain &terrain, const VoxelOctreeNode &chunk)
    : _chunk(&chunk), _cubicVoxels(terrain.get_cubic_voxels()), _meshChunk(StitchedMeshChunk(terrain, chunk))
{
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
        color += glm::mix(na->get_color(), nb->get_color(), t);
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

    vertexPosition /= static_cast<float>(edge_crossings);
    color /= static_cast<float>(edge_crossings);
    normal = glm::normalize(normal);
    if (_cubicVoxels)
        vertexPosition = _meshChunk.nodes[node_id]->_center;

    vertexPosition -= _chunk->_center;
    int vertexIndex = _verts.size();
    glm::ivec3 grid_position = _meshChunk.positions[node_id];
    if ((on_ring || _meshChunk.is_on_any_boundary(grid_position)) &&
        _meshChunk.should_have_boundary_quad(neighbours, on_ring))
    {
        if (on_ring)
            _ringEdgeNodes[grid_position] = node_id;
        else
            _innerEdgeNodes[grid_position] = node_id;
    }

    _meshChunk.vertexIndices[node_id] = vertexIndex;
    _verts.push_back({vertexPosition.x, vertexPosition.y, vertexPosition.z});
    _normals.push_back({normal.x, normal.y, normal.z});
    _colors.push_back({color.r, color.g, color.b, color.a});
}

ChunkMeshData *StitchedSurfaceNets::generate_mesh_data(const JarVoxelTerrain &terrain)
{
    for (size_t node_id = 0; node_id < _meshChunk.innerNodeCount; node_id++)
    {
        if (_meshChunk.vertexIndices[node_id] <= -2)
            continue;
        auto neighbours = std::vector<int>();
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
        auto neighbours = std::vector<int>();
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

            auto neighbours = std::vector<int>();
            if (_meshChunk.get_unique_neighbouring_vertices(pos, StitchedMeshChunk::FaceOffsets[i], neighbours) &&
                neighbours.size() == 4)
            {
                int n0 = _meshChunk.vertexIndices[neighbours[0]];
                int n1 = _meshChunk.vertexIndices[neighbours[1]];
                int n2 = _meshChunk.vertexIndices[neighbours[2]];
                int n3 = _meshChunk.vertexIndices[neighbours[3]];
                if (_verts[n0].distance_squared_to(_verts[n3]) < _verts[n1].distance_squared_to(_verts[n2]))
                {
                    add_tri(n0, n1, n3, flipFace == -1);
                    add_tri(n0, n3, n2, flipFace == -1);
                }
                else
                {
                    add_tri(n1, n3, n2, flipFace == -1);
                    add_tri(n1, n2, n0, flipFace == -1);
                }
            }
        }
    }

    if (_indices.size() == 0)    
        return nullptr;
    

    if (_meshChunk.is_edge_chunk())
    {
        // go through inner node edges, then if some inner node edge in +x/y/z exists, attempt to find a ring vertices
        // around this. make triangle/quad depending on what you find
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
                            add_tri_fix_normal(n0, n1, n3);
                            add_tri_fix_normal(n0, n3, n2);
                        }
                        else
                        {
                            add_tri_fix_normal(n1, n3, n2);
                            add_tri_fix_normal(n1, n2, n0);
                        }
                    }
                    else if (neighbours.size() == 1)
                    {
                        int n2 = _meshChunk.vertexIndices[neighbours[0]];
                        add_tri_fix_normal(n0, n1, n2);
                    }
                }
            }
        }
    }

    Array meshData;
    meshData.resize(Mesh::ARRAY_MAX);
    meshData[Mesh::ARRAY_VERTEX] = _verts;
    meshData[Mesh::ARRAY_NORMAL] = _normals;
    meshData[Mesh::ARRAY_COLOR] = _colors;
    meshData[Mesh::ARRAY_INDEX] = _indices;

    ChunkMeshData *output =
        new ChunkMeshData(meshData, _chunk->get_lod(), _meshChunk.is_edge_chunk(), _chunk->get_bounds(terrain.get_octree_scale()));
    output->boundaries = _meshChunk._lodH2LBoundaries | (_meshChunk._lodL2HBoundaries << 8);
    output->edgeVertices = _ringEdgeNodes;
    output->edgeVertices.insert(_innerEdgeNodes.begin(), _innerEdgeNodes.end());
    for (auto &[pos, node_id] : output->edgeVertices)
    {
        output->edgeVertices[pos] = _meshChunk.vertexIndices[node_id];
    }

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