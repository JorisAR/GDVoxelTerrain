#include "dual_surface_extractor.h"
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "utility/utils.h"
#include "voxel_terrain.h"

using namespace godot;

DualSurfaceExtractor::DualSurfaceExtractor(const JarVoxelTerrain &terrain)
{
    _cubicVoxels = (terrain.get_cubic_voxels());
    _split_by_material = terrain.get_material_mode() == VoxelMaterialMode::VOXEL_MATERIAL_MODE_MULTIPLE_MESHES;
}

void DualSurfaceExtractor::create_vertex(const JarVoxelTerrain &terrain, const int node_id,
                                         const std::vector<int> &neighbours, const bool on_ring)
{
    glm::vec3 vertexPosition(0.0f);
    glm::vec4 color(0, 0, 0, 0);
    glm::vec3 normal(0.0f);
    int duplicates = 0, edge_crossings = 0;
    auto material_mode = terrain.get_material_mode();
    for (auto &edge : MeshChunk::Edges)
    {
        auto ai = neighbours[edge.x];
        auto bi = neighbours[edge.y];
        auto na = _meshChunk->nodes[ai];
        auto nb = _meshChunk->nodes[bi];
        float valueA = na->get_value();
        float valueB = nb->get_value();
        glm::vec3 posA = na->get_center();
        glm::vec3 posB = nb->get_center();

        normal += (valueB - valueA) * (posB - posA);

        if (glm::sign(valueA) == glm::sign(valueB))
            continue;

        float t = glm::abs(valueA) / (glm::abs(valueA) + glm::abs(valueB));
        vertexPosition += glm::mix(posA, posB, t);
        edge_crossings++;

        auto colorA = VoxelTerrainMaterial::get_color_from_material(material_mode, na->get_material_index());
        auto colorB = VoxelTerrainMaterial::get_color_from_material(material_mode, nb->get_material_index());

        color += glm::mix(colorA, colorB, t);
    }

    if (edge_crossings <= 0)
        return;

    // computes and stores the directions in which to generate quads, also determines winding order
    _meshChunk->faceDirs[node_id] =
        (static_cast<int>(glm::sign(glm::sign(_meshChunk->nodes[neighbours[6]]->get_value()) -
                                    glm::sign(_meshChunk->nodes[neighbours[7]]->get_value())) +
                          1))
            << 0 |
        (static_cast<int>(glm::sign(glm::sign(_meshChunk->nodes[neighbours[7]]->get_value()) -
                                    glm::sign(_meshChunk->nodes[neighbours[5]]->get_value())) +
                          1))
            << 2 |
        (static_cast<int>(glm::sign(glm::sign(_meshChunk->nodes[neighbours[3]]->get_value()) -
                                    glm::sign(_meshChunk->nodes[neighbours[7]]->get_value())) +
                          1))
            << 4;

    vertexPosition /= static_cast<float>(edge_crossings);
    color /= static_cast<float>(edge_crossings);
    normal = glm::normalize(normal);
    if (_cubicVoxels)
        vertexPosition = _meshChunk->nodes[node_id]->get_center();

    vertexPosition -= _chunk->get_center();

    glm::ivec3 grid_position = _meshChunk->positions[node_id];
    if ((on_ring || _meshChunk->is_on_any_boundary(grid_position)) &&
        _meshChunk->should_have_boundary_quad(neighbours, on_ring))
    {
        if (on_ring)
            _ringEdgeNodes[grid_position] = node_id;
        else
            _innerEdgeNodes[grid_position] = node_id;
    }

    int vertexIndex = _mainSurface.add_vertex({vertexPosition.x, vertexPosition.y, vertexPosition.z},
                                              {normal.x, normal.y, normal.z}, {color.r, color.g, color.b, color.a});

    _meshChunk->vertexIndices[node_id] = vertexIndex;
}

ExtractedMeshData *DualSurfaceExtractor::generate_mesh_data(const JarVoxelTerrain &terrain,
                                                            const VoxelOctreeNode &chunk)
{
    _meshChunk = std::make_unique<MeshChunk>(terrain, chunk);
    _chunk = const_cast<VoxelOctreeNode *>(&chunk);

    for (size_t node_id = 0; node_id < _meshChunk->innerNodeCount; node_id++)
    {
        if (_meshChunk->vertexIndices[node_id] <= -2)
            continue;
        auto neighbours = std::vector<int>();
        glm::ivec3 grid_position = _meshChunk->positions[node_id];

        if (!_meshChunk->get_neighbours(grid_position, neighbours))
            continue;
        create_vertex(terrain, node_id, neighbours, false);
    }

    if (_mainSurface.no_vertices())
        return nullptr;

    // if on lod boundary, add an additional pass
    for (size_t node_id = _meshChunk->innerNodeCount; node_id < _meshChunk->innerNodeCount + _meshChunk->ringNodeCount;
         node_id++)
    {
        if (_meshChunk->vertexIndices[node_id] <= -2)
            continue;
        auto neighbours = std::vector<int>();
        glm::ivec3 grid_position = _meshChunk->positions[node_id];

        if (!_meshChunk->get_ring_neighbours(grid_position, neighbours))
            continue;
        create_vertex(terrain, node_id, neighbours, true);
        // if (false)
        // { // print the nodes itself
        //     glm::vec3 vertexPosition = _meshChunk->nodes[node_id]->get_center();
        //     vertexPosition -= _chunk->get_center();
        //     int vertexIndex = (_verts.size());
        //     _meshChunk->vertexIndices[node_id] = vertexIndex;
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

    stitch_inner_faces();
    if (!_split_by_material && _mainSurface.no_indices())
        return nullptr;
    stitch_edge_chunk_boundaries();
    return build_chunk_mesh_data(terrain);
}

void DualSurfaceExtractor::stitch_inner_faces()
{
    for (size_t node_id = 0; node_id < _meshChunk->innerNodeCount; ++node_id)
    {
        if (_meshChunk->vertexIndices[node_id] <= -1)
            continue;

        const auto pos = _meshChunk->positions[node_id];
        const int faceDirs = _meshChunk->faceDirs[node_id];

        static const int faces = 3;
        for (int i = 0; i < faces; ++i)
        {
            int flipFace = ((faceDirs >> (2 * i)) & 3) - 1;
            if (flipFace == 0 || !_meshChunk->should_have_quad(pos, i))
                continue;

            std::vector<int> neighbours;
            if (_meshChunk->get_unique_neighbouring_vertices(pos, MeshChunk::FaceOffsets[i], neighbours) &&
                neighbours.size() == 4)
            {
                int n0 = _meshChunk->vertexIndices[neighbours[0]];
                int n1 = _meshChunk->vertexIndices[neighbours[1]];
                int n2 = _meshChunk->vertexIndices[neighbours[2]];
                int n3 = _meshChunk->vertexIndices[neighbours[3]];

                auto windingPolicy = (flipFace != 1) ? WindingPolicy::ForceCW : WindingPolicy::ForceCCW;
                uint32_t material_index = 0;
                if (_split_by_material)
                    material_index = _meshChunk->nodes[neighbours[0]]->get_material_index();

                add_quad_to_surface(n0, n1, n2, n3, windingPolicy, material_index);
            }
        }
    }
}

void DualSurfaceExtractor::stitch_edge_chunk_boundaries()
{
    if (!_meshChunk->is_edge_chunk())
        return;

    static const int faces = 3;
    static const glm::ivec3 offsets[3] = {glm::ivec3(1, 0, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, 1)};

    for (auto &[pos, node_id] : _innerEdgeNodes)
    {
        for (int i = 0; i < faces; ++i)
        {
            int innerNeighbour = node_id;
            if (!(_meshChunk->on_positive_edge(pos)))
            {
                auto it = _innerEdgeNodes.find(pos + offsets[i]);
                if (it == _innerEdgeNodes.end() || it->second < 0)
                    continue;
                innerNeighbour = it->second;
            }

            std::vector<std::vector<int>> neighboursLists = find_ring_nodes(pos, i);
            int n0 = _meshChunk->vertexIndices[node_id];
            int n1 = _meshChunk->vertexIndices[innerNeighbour];
            uint32_t material_index = _meshChunk->nodes[node_id]->get_material_index();

            for (auto &neighbours : neighboursLists)
            {
                if (neighbours.size() == 2)
                {
                    int n2 = _meshChunk->vertexIndices[neighbours[0]];
                    int n3 = _meshChunk->vertexIndices[neighbours[1]];
                    add_quad_to_surface(n0, n1, n2, n3, WindingPolicy::FromNormalCheck, material_index);
                }
                else if (neighbours.size() == 1)
                {
                    int n2 = _meshChunk->vertexIndices[neighbours[0]];
                    add_triangle_to_surface(n0, n1, n2, WindingPolicy::FromNormalCheck, material_index);
                }
            }
        }
    }
}

ExtractedMeshData *DualSurfaceExtractor::build_chunk_mesh_data(const JarVoxelTerrain &terrain) const
{
    std::vector<Array> mesh_arrays;
    std::vector<int> material_indices;
    if (_split_by_material)
    {
        for (auto &[material_index, surface] : _surfaces)
        {
            if (surface.no_indices())
                continue;
            mesh_arrays.push_back(surface.get_mesh_array());
            material_indices.push_back(material_index);
        }
    }
    else
    {
        if (!_mainSurface.no_indices())
            mesh_arrays.push_back(_mainSurface.get_mesh_array());
        material_indices.push_back(0);
    }

    if (mesh_arrays.empty())
    {
        return nullptr;
    }

    auto *output = new ExtractedMeshData(mesh_arrays, material_indices, _chunk->get_lod(), _meshChunk->is_edge_chunk(),
                                         _chunk->get_bounds(terrain.get_octree_scale()));

    output->boundaries = _meshChunk->_lodH2LBoundaries | (_meshChunk->_lodL2HBoundaries << 8);
    output->edgeVertices = _ringEdgeNodes;
    output->edgeVertices.insert(_innerEdgeNodes.begin(), _innerEdgeNodes.end());
    for (auto &[pos, node_id] : output->edgeVertices)
    {
        output->edgeVertices[pos] = _meshChunk->vertexIndices[node_id];
    }
    return output;
}

void godot::DualSurfaceExtractor::add_triangle_to_surface(int n0, int n1, int n2, WindingPolicy policy,
                                                          uint32_t material_index)
{
    if (!_split_by_material)
    {
        _mainSurface.add_triangle(n0, n1, n2, policy);
        return;
    }
    MeshSurface &surface = get_surface_for_material(material_index);
    surface.add_triangle(n0, n1, n2, _mainSurface, policy);
}

void godot::DualSurfaceExtractor::add_quad_to_surface(int n0, int n1, int n2, int n3, WindingPolicy policy,
                                                      uint32_t material_index)
{
    if (!_split_by_material)
    {
        _mainSurface.add_quad(n0, n1, n2, n3, policy);
        return;
    }
    MeshSurface &surface = get_surface_for_material(material_index);
    surface.add_quad(n0, n1, n2, n3, _mainSurface, policy);
}

MeshSurface &godot::DualSurfaceExtractor::get_surface_for_material(uint32_t material_index)
{
    auto it = _surfaces.find(material_index);
    if (it != _surfaces.end())
        return it->second;
    auto [new_it, _] = _surfaces.emplace(material_index, MeshSurface());
    return new_it->second;
}

// this function finds way too many possible nodes.
// Possible improvement: check the octant, e.g. we dont need to find ringnodes towards neighbours of the same LOD
// Possible improvement: use the same system of a crossed edge as before to verify if we need a quad or not
std::vector<std::vector<int>> DualSurfaceExtractor::find_ring_nodes(const glm::ivec3 &pos, const int face) const
{
    static const glm::ivec3 face_offsets[3] = {glm::ivec3(1, 0, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, 0, 1)};
    static const glm::ivec3 ring_offsets[3][8] = {
        {glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1), glm::ivec3(0, 1, 1),
         glm::ivec3(0, -1, -1), glm::ivec3(0, -1, 1), glm::ivec3(0, 1, -1)},
        {glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0), glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1), glm::ivec3(1, 0, 1),
         glm::ivec3(-1, 0, -1), glm::ivec3(-1, 0, 1), glm::ivec3(1, 0, -1)},
        {glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0), glm::ivec3(1, 1, 0),
         glm::ivec3(-1, -1, 0), glm::ivec3(-1, 1, 0), glm::ivec3(1, -1, 0)}};

    // static const glm::ivec3 ring_offsets[3][4] = {
    //     {glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1)},
    //     {glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0), glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1)},
    //     {glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0), glm::ivec3(0, -1, 0)}};
    // function to go from inner coordinates to ring coordinates
    auto get_ring_node = [this](const glm::ivec3 pos)
    {
        glm::ivec3 ring_pos = glm::floor((glm::vec3(pos)) / 2.0f);

        auto it = _ringEdgeNodes.find(ring_pos);
        if (it == _ringEdgeNodes.end() || it->second < 0 || _meshChunk->vertexIndices[it->second] < 0)
            return -1;

        return it->second;
    };

    std::vector<std::vector<int>> result;
    for (size_t i = 0; i < 3; i++) // check all directions?
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
