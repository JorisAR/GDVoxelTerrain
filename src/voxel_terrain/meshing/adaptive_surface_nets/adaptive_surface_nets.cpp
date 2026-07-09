#include "adaptive_surface_nets.h"
#include "fit_plane.h"
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "voxel_terrain.h"
AdaptiveSurfaceNets::AdaptiveSurfaceNets(const JarVoxelTerrain &terrain, const VoxelOctreeNode &chunk)
    : _chunk(&chunk), _meshChunk(AdaptiveMeshChunk(terrain, chunk))
{
    int vertCount = _meshChunk.nodes.size();
    // _verts.reserve(vertCount);
    // _normals.reserve(vertCount);
    // _colors.reserve(vertCount);
    // _indices.reserve(vertCount * 6);
}

ChunkMeshData *AdaptiveSurfaceNets::generate_mesh_data(const JarVoxelTerrain &terrain)
{
    // if(_meshChunk.is_edge_chunk())
    //      return nullptr;

    int flip = _meshChunk.Octant.x * _meshChunk.Octant.y * _meshChunk.Octant.z;
    glm::vec3 half_leaf_size = _meshChunk.get_half_leaf_size() * glm::vec3(_meshChunk.Octant);
    
    for (size_t node_id = 0; node_id < _meshChunk.nodes.size(); node_id++)
    {
        if (_meshChunk.vertexIndices[node_id] <= -2)
            continue;
        auto neighbours = std::vector<int>();

        if (!_meshChunk.get_neighbours(_meshChunk.positions[node_id], neighbours))
            continue;
         
        glm::vec3 vertexPosition(0.0f);
        Color color = Color(0, 0, 0, 0);
        glm::vec3 normal(0.0f);
        int duplicates = 0, edge_crossings = 0;
        for (auto &edge : AdaptiveMeshChunk::Edges)
        {
            auto ai = neighbours[edge.x];
            auto bi = neighbours[edge.y];
            if (ai == bi)
            {
                duplicates++;
                continue;
            }
            auto na = _meshChunk.nodes[ai];
            auto nb = _meshChunk.nodes[bi];

            float valueA = na->get_value();
            float valueB = nb->get_value();
            glm::vec3 posA = na->_center;
            glm::vec3 posB = nb->_center;
            // glm::vec3 nPosA = _meshChunk.Offsets[edge.x];
            // glm::vec3 nPosB = _meshChunk.Offsets[edge.y];

            normal += (valueB - valueA) * (posB - posA);
            // normal += (valueB - valueA) * (nPosB - nPosA);

            if (glm::sign(valueA) == glm::sign(valueB))
                continue;

            // Color colorA = na->get_node_color();
            // Color colorB = nb->get_node_color();

            float t = glm::abs(valueA) / (glm::abs(valueA) + glm::abs(valueB));
            glm::vec3 point = glm::mix(posA, posB, t);
            vertexPosition += point;
            _tempPoints[edge_crossings++] = point;
            // color += color.linear_interpolate(colorA.linear_interpolate(colorB, t), 1.0f);
        }

        if (edge_crossings <= 0)
        {
            // UtilityFunctions::print("No crossings!");
            continue;
        }
           

        _meshChunk.faceDirs[node_id] =
            (static_cast<int>(flip * glm::sign(glm::sign(_meshChunk.nodes[neighbours[6]]->get_value()) -
                                               glm::sign(_meshChunk.nodes[neighbours[7]]->get_value())) +
                              1))
                << 0 |
            (static_cast<int>(flip * glm::sign(glm::sign(_meshChunk.nodes[neighbours[7]]->get_value()) -
                                               glm::sign(_meshChunk.nodes[neighbours[5]]->get_value())) +
                              1))
                << 2 |
            (static_cast<int>(flip * glm::sign(glm::sign(_meshChunk.nodes[neighbours[3]]->get_value()) -
                                               glm::sign(_meshChunk.nodes[neighbours[7]]->get_value())) +
                              1))
                << 4;

        vertexPosition /= static_cast<float>(edge_crossings);
        color /= static_cast<float>(edge_crossings);
        normal = glm::normalize(normal);

        // if (SquareVoxels)
        // vertexPosition = _meshChunk.nodes[node_id]->_center;

         //if we have duplicate nodes (on lod boundaries), normals cannot be accurately computed. instead, average the neighbour normals for a good approximation. 
        bool badNormal = duplicates > 0;
        _badNormals.push_back(badNormal);

        if (badNormal)
        {
            normal = glm::vec3(0,0,0);
        }

        vertexPosition -= _chunk->_center;
        _meshChunk.vertexIndices[node_id] = (_verts.size());
        _verts.push_back({vertexPosition.x, vertexPosition.y, vertexPosition.z});
        _normals.push_back({normal.x, normal.y, normal.z});
        _colors.push_back(color);
    }

    if (_verts.size() == 0)
    {
        // UtilityFunctions::printerr("No vertices!");
        return nullptr;
    }

    for (size_t node_id = 0; node_id < _meshChunk.nodes.size(); node_id++)
    {
        if (_meshChunk.vertexIndices[node_id] <= -1)
            continue;

        const int faces = 3;
        auto pos = _meshChunk.positions[node_id];
        auto isSmall = _meshChunk.isSmalls[node_id];
        auto faceDirs = _meshChunk.faceDirs[node_id];
        for (int i = 0; i < faces; i++)
        {
            int flipFace = ((faceDirs >> (2 * i)) & 3) - 1;
            if (flipFace == 0 || !_meshChunk.should_have_quad(pos, i, isSmall))
                continue;

            auto neighbours = std::vector<int>();
            if (_meshChunk.get_unique_neighbouring_vertices(pos, AdaptiveMeshChunk::FaceOffsets[i], neighbours))
            {
                if (neighbours.size() == 4)
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
                else if (neighbours.size() == 3)
                {
                    if (_meshChunk.nodes[neighbours[0]]->_size == _meshChunk.nodes[neighbours[1]]->_size &&
                        _meshChunk.nodes[neighbours[1]]->_size == _meshChunk.nodes[neighbours[2]]->_size)
                        continue;

                    add_tri(_meshChunk.vertexIndices[neighbours[0]], _meshChunk.vertexIndices[neighbours[1]],
                            _meshChunk.vertexIndices[neighbours[2]], flipFace == -1);
                }
            }
        }
    }

    if (_indices.size() == 0)
    {
        // UtilityFunctions::printerr("No indices!");
        return nullptr;
    }

    for (size_t vert_id = 0; vert_id < _normals.size(); vert_id++)
    {
        _normals[vert_id] = _normals[vert_id].normalized();
    }

    // Assign arrays to mesh data
    Array meshData;
    meshData.resize(Mesh::ARRAY_MAX);
    meshData[Mesh::ARRAY_VERTEX] = _verts;
    meshData[Mesh::ARRAY_NORMAL] = _normals;
    meshData[Mesh::ARRAY_COLOR] = _colors;
    meshData[Mesh::ARRAY_INDEX] = _indices;

    return new ChunkMeshData(meshData, _meshChunk.get_real_lod(), _meshChunk.is_edge_chunk(),
                             _chunk->get_bounds(terrain.get_octree_scale()));
}

void AdaptiveSurfaceNets::add_tri(int n0, int n1, int n2, bool flip)
{
    // if (_meshChunk.is_edge_chunk()) {
        // For each vertex in the triangle, only accumulate from good neighbors
    auto safe_accumulate = [&](int target, int a, int b) {
        if (_badNormals[target]) {  
            // Only use neighbor if it's not marked bad
            if (!_badNormals[a]) {
                _normals[target] += _normals[a];
            }
            if (!_badNormals[b]) {
                _normals[target] += _normals[b];
            }
        }
    };

    if(_meshChunk.is_edge_chunk() && _badNormals[n0] && _badNormals[n1] && _badNormals[n2]) return;

    safe_accumulate(n0, n1, n2);
    safe_accumulate(n1, n0, n2);
    safe_accumulate(n2, n0, n1);

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