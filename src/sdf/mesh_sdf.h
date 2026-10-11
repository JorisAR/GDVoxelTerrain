#ifndef MESH_SDF_H
#define MESH_SDF_H

#include "signed_distance_field.h"
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <vector>

// Signed distance field of a closed triangle mesh, for stamping arbitrary
// shapes into volumes. The mesh is baked once into a regular distance grid
// (stored in the resource, so saved .tres files need no re-bake) and sampled
// with trilinear interpolation, which is fast and thread-safe.
// Inside/outside is decided by ray parity along all three axes with a majority
// vote, which tolerates small holes and non-manifold spots in the mesh.
class JarMeshSdf : public JarSignedDistanceField
{
    GDCLASS(JarMeshSdf, JarSignedDistanceField);

  private:
    Ref<Mesh> _mesh;
    int _resolution = 64;   // grid cells along the mesh's longest axis
    float _padding = 2.0f;  // empty cells around the mesh

    // Baked grid (sample points at cell corners).
    std::vector<float> _grid;
    glm::ivec3 _size{0};
    glm::vec3 _origin{0.0f};
    float _cellSize = 1.0f;
    Bounds _meshBounds;
    bool _dirty = true;

    float sample_grid(const glm::vec3 &g) const;

  public:
    void set_mesh(const Ref<Mesh> &mesh);
    Ref<Mesh> get_mesh() const { return _mesh; }
    void set_resolution(int value);
    int get_resolution() const { return _resolution; }
    void set_padding(float value);
    float get_padding() const { return _padding; }

    // Main thread. Rebuilds the grid from the mesh. Called automatically by
    // prepare() when the mesh or settings changed and no baked data is stored.
    void bake();
    bool is_baked() const { return !_grid.empty(); }
    Callable get_bake_button();

    float distance(const glm::vec3 &pos) const override;
    Bounds bounds() const override;
    void prepare() override;

    // Storage of the baked grid (not shown in the inspector).
    PackedFloat32Array get_baked_data() const;
    void set_baked_data(const PackedFloat32Array &data);
    Vector3i get_baked_size() const { return Vector3i(_size.x, _size.y, _size.z); }
    void set_baked_size(const Vector3i &v) { _size = glm::ivec3(v.x, v.y, v.z); }
    Vector3 get_baked_origin() const { return Vector3(_origin.x, _origin.y, _origin.z); }
    void set_baked_origin(const Vector3 &v) { _origin = glm::vec3(v.x, v.y, v.z); }
    float get_baked_cell_size() const { return _cellSize; }
    void set_baked_cell_size(float v) { _cellSize = v; }
    AABB get_baked_mesh_bounds() const;
    void set_baked_mesh_bounds(const AABB &aabb) { _meshBounds = Bounds(aabb); }

  protected:
    static void _bind_methods();
};

#endif // MESH_SDF_H
