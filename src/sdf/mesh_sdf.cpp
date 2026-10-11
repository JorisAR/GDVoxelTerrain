#include "mesh_sdf.h"
#include "job_system.h"
#include "triangle_bvh.h"
#include <algorithm>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

void JarMeshSdf::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("set_mesh", "mesh"), &JarMeshSdf::set_mesh);
    ClassDB::bind_method(D_METHOD("get_mesh"), &JarMeshSdf::get_mesh);
    ClassDB::bind_method(D_METHOD("set_resolution", "resolution"), &JarMeshSdf::set_resolution);
    ClassDB::bind_method(D_METHOD("get_resolution"), &JarMeshSdf::get_resolution);
    ClassDB::bind_method(D_METHOD("set_padding", "cells"), &JarMeshSdf::set_padding);
    ClassDB::bind_method(D_METHOD("get_padding"), &JarMeshSdf::get_padding);
    ClassDB::bind_method(D_METHOD("bake"), &JarMeshSdf::bake);
    ClassDB::bind_method(D_METHOD("is_baked"), &JarMeshSdf::is_baked);
    ClassDB::bind_method(D_METHOD("get_bake_button"), &JarMeshSdf::get_bake_button);

    ClassDB::bind_method(D_METHOD("set_baked_data", "data"), &JarMeshSdf::set_baked_data);
    ClassDB::bind_method(D_METHOD("get_baked_data"), &JarMeshSdf::get_baked_data);
    ClassDB::bind_method(D_METHOD("set_baked_size", "size"), &JarMeshSdf::set_baked_size);
    ClassDB::bind_method(D_METHOD("get_baked_size"), &JarMeshSdf::get_baked_size);
    ClassDB::bind_method(D_METHOD("set_baked_origin", "origin"), &JarMeshSdf::set_baked_origin);
    ClassDB::bind_method(D_METHOD("get_baked_origin"), &JarMeshSdf::get_baked_origin);
    ClassDB::bind_method(D_METHOD("set_baked_cell_size", "size"), &JarMeshSdf::set_baked_cell_size);
    ClassDB::bind_method(D_METHOD("get_baked_cell_size"), &JarMeshSdf::get_baked_cell_size);
    ClassDB::bind_method(D_METHOD("set_baked_mesh_bounds", "bounds"), &JarMeshSdf::set_baked_mesh_bounds);
    ClassDB::bind_method(D_METHOD("get_baked_mesh_bounds"), &JarMeshSdf::get_baked_mesh_bounds);

    ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh"), "set_mesh", "get_mesh");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "resolution", PROPERTY_HINT_RANGE, "4,256,1"), "set_resolution",
                 "get_resolution");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "padding", PROPERTY_HINT_RANGE, "1,16,0.5"), "set_padding",
                 "get_padding");
    ADD_PROPERTY(PropertyInfo(Variant::CALLABLE, "bake_now", PROPERTY_HINT_TOOL_BUTTON, "Bake", PROPERTY_USAGE_EDITOR),
                 "", "get_bake_button");

    // Baked grid: saved with the resource, hidden from the inspector.
    const uint32_t storage = PROPERTY_USAGE_STORAGE | PROPERTY_USAGE_NO_EDITOR;
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3I, "baked_size", PROPERTY_HINT_NONE, "", storage), "set_baked_size",
                 "get_baked_size");
    ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "baked_origin", PROPERTY_HINT_NONE, "", storage), "set_baked_origin",
                 "get_baked_origin");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "baked_cell_size", PROPERTY_HINT_NONE, "", storage),
                 "set_baked_cell_size", "get_baked_cell_size");
    ADD_PROPERTY(PropertyInfo(Variant::AABB, "baked_mesh_bounds", PROPERTY_HINT_NONE, "", storage),
                 "set_baked_mesh_bounds", "get_baked_mesh_bounds");
    // Data last: it validates against the size above when loaded.
    ADD_PROPERTY(PropertyInfo(Variant::PACKED_FLOAT32_ARRAY, "baked_data", PROPERTY_HINT_NONE, "", storage),
                 "set_baked_data", "get_baked_data");
}

void JarMeshSdf::set_mesh(const Ref<Mesh> &mesh)
{
    _mesh = mesh;
    _dirty = true;
    emit_changed();
}

void JarMeshSdf::set_resolution(int value)
{
    _resolution = std::clamp(value, 4, 256);
    _dirty = true;
    emit_changed();
}

void JarMeshSdf::set_padding(float value)
{
    _padding = std::max(1.0f, value);
    _dirty = true;
    emit_changed();
}

Callable JarMeshSdf::get_bake_button()
{
    return callable_mp(this, &JarMeshSdf::bake);
}

PackedFloat32Array JarMeshSdf::get_baked_data() const
{
    PackedFloat32Array data;
    data.resize(_grid.size());
    std::copy(_grid.begin(), _grid.end(), data.ptrw());
    return data;
}

void JarMeshSdf::set_baked_data(const PackedFloat32Array &data)
{
    _grid.assign(data.ptr(), data.ptr() + data.size());
    // Stored data matches the stored mesh: no need to re-bake on prepare().
    _dirty = _grid.size() != size_t(_size.x) * _size.y * _size.z;
}

AABB JarMeshSdf::get_baked_mesh_bounds() const
{
    if (!_meshBounds.is_valid())
        return AABB();
    const glm::vec3 s = _meshBounds.get_size();
    return AABB(Vector3(_meshBounds.min.x, _meshBounds.min.y, _meshBounds.min.z), Vector3(s.x, s.y, s.z));
}

void JarMeshSdf::prepare()
{
    if (_dirty || _grid.empty())
        bake();
}

void JarMeshSdf::bake()
{
    _dirty = false;
    _grid.clear();
    _size = glm::ivec3(0);
    if (_mesh.is_null())
        return;

    // Triangle soup from every triangle surface of the mesh.
    std::vector<glm::vec3> corners;
    for (int s = 0; s < _mesh->get_surface_count(); ++s)
    {
        // Primitive meshes are always triangles; array meshes may contain lines/points.
        const ArrayMesh *arrayMesh = Object::cast_to<ArrayMesh>(_mesh.ptr());
        if (arrayMesh && arrayMesh->surface_get_primitive_type(s) != Mesh::PRIMITIVE_TRIANGLES)
            continue;
        const Array arrays = _mesh->surface_get_arrays(s);
        const PackedVector3Array verts = arrays[Mesh::ARRAY_VERTEX];
        const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
        const int64_t count = indices.is_empty() ? verts.size() : indices.size();
        for (int64_t i = 0; i + 2 < count; i += 3)
            for (int k = 0; k < 3; ++k)
            {
                const Vector3 v = verts[indices.is_empty() ? i + k : indices[i + k]];
                corners.emplace_back(v.x, v.y, v.z);
            }
    }
    if (corners.empty())
    {
        UtilityFunctions::push_warning("JarMeshSdf: mesh has no triangles to bake.");
        return;
    }

    TriangleBvh bvh;
    bvh.build(corners);
    _meshBounds = bvh.bounds();

    const glm::vec3 extent = _meshBounds.get_size();
    const float longest = std::max(extent.x, std::max(extent.y, extent.z));
    _cellSize = std::max(longest, 1e-4f) / float(_resolution);
    const glm::vec3 pad(_padding * _cellSize);
    _origin = _meshBounds.min - pad;
    _size = glm::ivec3(glm::ceil((extent + 2.0f * pad) / _cellSize)) + glm::ivec3(1);
    const size_t total = size_t(_size.x) * _size.y * _size.z;
    auto index = [this](int x, int y, int z) { return size_t(x) + size_t(_size.x) * (size_t(y) + size_t(_size.y) * z); };

    // Unsigned distance to the closest triangle, one z-slice per task.
    std::vector<float> distance(total);
    JobSystem::get_pool().parallel_for(_size.z, [&](size_t z) {
        for (int y = 0; y < _size.y; ++y)
            for (int x = 0; x < _size.x; ++x)
                distance[index(x, y, int(z))] =
                    bvh.closest_distance(_origin + glm::vec3(x, y, int(z)) * _cellSize);
    });

    // Inside votes: parity of ray crossings along each axis. Rays are nudged off
    // the grid lines so they rarely pass exactly through mesh edges or vertices.
    std::vector<uint8_t> votes(total, 0);
    for (int axis = 0; axis < 3; ++axis)
    {
        const int u = (axis + 1) % 3, v = (axis + 2) % 3;
        JobSystem::get_pool().parallel_for(_size[v], [&](size_t vi) {
            std::vector<float> hits;
            for (int ui = 0; ui < _size[u]; ++ui)
            {
                glm::vec3 origin = _origin;
                origin[u] += ui * _cellSize + _cellSize * 1.37e-4f;
                origin[v] += int(vi) * _cellSize + _cellSize * 2.91e-4f;
                origin[axis] -= _cellSize;
                hits.clear();
                bvh.axis_ray_hits(origin, axis, hits);
                std::sort(hits.begin(), hits.end());
                size_t crossed = 0;
                for (int ai = 0; ai < _size[axis]; ++ai)
                {
                    const float t = (ai + 1) * _cellSize; // grid point ai along the ray
                    while (crossed < hits.size() && hits[crossed] < t)
                        ++crossed;
                    if (crossed & 1)
                    {
                        glm::ivec3 g;
                        g[axis] = ai;
                        g[u] = ui;
                        g[v] = int(vi);
                        votes[index(g.x, g.y, g.z)]++; // each (u, v, axis) cell is written by one task only
                    }
                }
            }
        });
    }

    _grid.resize(total);
    for (size_t i = 0; i < total; ++i)
        _grid[i] = votes[i] >= 2 ? -distance[i] : distance[i];
    emit_changed();
}

float JarMeshSdf::sample_grid(const glm::vec3 &g) const
{
    const glm::ivec3 i0 = glm::clamp(glm::ivec3(glm::floor(g)), glm::ivec3(0), _size - glm::ivec3(2));
    const glm::vec3 f = glm::clamp(g - glm::vec3(i0), glm::vec3(0.0f), glm::vec3(1.0f));
    auto at = [this](int x, int y, int z) {
        return _grid[size_t(x) + size_t(_size.x) * (size_t(y) + size_t(_size.y) * z)];
    };
    const float c00 = glm::mix(at(i0.x, i0.y, i0.z), at(i0.x + 1, i0.y, i0.z), f.x);
    const float c10 = glm::mix(at(i0.x, i0.y + 1, i0.z), at(i0.x + 1, i0.y + 1, i0.z), f.x);
    const float c01 = glm::mix(at(i0.x, i0.y, i0.z + 1), at(i0.x + 1, i0.y, i0.z + 1), f.x);
    const float c11 = glm::mix(at(i0.x, i0.y + 1, i0.z + 1), at(i0.x + 1, i0.y + 1, i0.z + 1), f.x);
    return glm::mix(glm::mix(c00, c10, f.y), glm::mix(c01, c11, f.y), f.z);
}

float JarMeshSdf::distance(const glm::vec3 &pos) const
{
    if (_grid.empty() || glm::any(glm::lessThan(_size, glm::ivec3(2))))
        return 1e9f;
    const glm::vec3 gridMax = _origin + glm::vec3(_size - glm::ivec3(1)) * _cellSize;
    const glm::vec3 clamped = glm::clamp(pos, _origin, gridMax);
    const float border = sample_grid((clamped - _origin) / _cellSize);
    if (clamped == pos)
        return border;
    // Outside the grid, return a lower bound (never overshooting the surface,
    // which matters for ray marching): the best of the distance to the mesh's
    // box and the border value minus the distance to the border.
    const glm::vec3 toBox = glm::max(glm::max(_meshBounds.min - pos, pos - _meshBounds.max), glm::vec3(0.0f));
    return std::max(glm::length(toBox), border - glm::length(pos - clamped));
}

Bounds JarMeshSdf::bounds() const
{
    return is_baked() ? _meshBounds : Bounds();
}
