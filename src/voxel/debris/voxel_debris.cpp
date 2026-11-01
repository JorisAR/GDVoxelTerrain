#include "voxel_debris.h"
#include "block_mesher.h"
#include "utility/job_system.h"
#include "voxel_chunk.h"
#include "voxel_volume.h"
#include "universe/voxel_universe.h"
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/convex_polygon_shape3d.hpp>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/core/object.hpp>

void JarVoxelDebrisBody::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("get_voxel_count"), &JarVoxelDebrisBody::get_voxel_count);
    ClassDB::bind_method(D_METHOD("is_merging"), &JarVoxelDebrisBody::is_merging);
}

// Falls with the universe's gravity (that of the body the viewer is on, plus
// tides), like the player. The space's own point gravity cannot do it under
// Jolt: it pulled every body as if it were at the world origin (pieces shot
// off a planet and hung above a moon). The engine's gravity is off for these
// bodies then (gravity_scale 0, see JarVoxelDebris::spawn).
void JarVoxelDebrisBody::_integrate_forces(PhysicsDirectBodyState3D *state)
{
    const JarVoxelUniverse *universe = JarVoxelUniverse::get_singleton();
    const bool own = universe != nullptr && universe->has_bodies();
    set_gravity_scale(own ? 0.0f : 1.0f);
    if (own)
        state->set_linear_velocity(state->get_linear_velocity() +
                                   universe->get_gravity(state->get_transform().origin) * state->get_step());
}

void JarVoxelDebris::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("merge_debris", "body"), &JarVoxelDebris::merge_debris);
    ClassDB::bind_method(D_METHOD("get_bodies"), &JarVoxelDebris::get_bodies);
#define BIND_DEBRIS(m_name, m_type, m_hint, m_hint_text)                                                              \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &JarVoxelDebris::get_##m_name);                                     \
    ClassDB::bind_method(D_METHOD("set_" #m_name, "value"), &JarVoxelDebris::set_##m_name);                            \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name, m_hint, m_hint_text), "set_" #m_name, "get_" #m_name)
    BIND_DEBRIS(enabled, Variant::BOOL, PROPERTY_HINT_NONE, "");
    BIND_DEBRIS(max_size, Variant::INT, PROPERTY_HINT_RANGE, "1,128,1,suffix:voxels");
    BIND_DEBRIS(merge, Variant::BOOL, PROPERTY_HINT_NONE, "");
    BIND_DEBRIS(merge_delay, Variant::FLOAT, PROPERTY_HINT_RANGE, "0,10,0.05,suffix:s");
    BIND_DEBRIS(density, Variant::FLOAT, PROPERTY_HINT_RANGE, "1,20000,1,suffix:kg/m3");
    BIND_DEBRIS(max_bodies, Variant::INT, PROPERTY_HINT_RANGE, "1,256,1");
    BIND_DEBRIS(collision_layer, Variant::INT, PROPERTY_HINT_LAYERS_3D_PHYSICS, "");
    BIND_DEBRIS(collision_mask, Variant::INT, PROPERTY_HINT_LAYERS_3D_PHYSICS, "");
#undef BIND_DEBRIS
    const PropertyInfo body(Variant::OBJECT, "body", PROPERTY_HINT_NODE_TYPE, "JarVoxelDebrisBody");
    ADD_SIGNAL(MethodInfo("debris_detached", body));
    ADD_SIGNAL(MethodInfo("debris_settled", body));
    ADD_SIGNAL(MethodInfo("debris_merged", body));
}

JarVoxelDebris::~JarVoxelDebris()
{
    detach();
}

void JarVoxelDebris::_notification(int what)
{
    if (what == NOTIFICATION_ENTER_TREE)
        attach();
    else if (what == NOTIFICATION_EXIT_TREE)
        detach();
}

void JarVoxelDebris::attach()
{
    if (Engine::get_singleton()->is_editor_hint())
        return; // nothing falls in the editor
    _volume = Object::cast_to<JarVoxelVolume>(get_parent());
    if (_volume)
        _volume->add_extension(this);
}

void JarVoxelDebris::detach()
{
    if (_volume)
        _volume->remove_extension(this); // waits until no hook of ours runs
    _volume = nullptr;
}

// A pool thread (see VolumeExtension::edit_applied).
void JarVoxelDebris::edit_applied(JarVoxelVolume &volume, const ModifySettings &edit)
{
    const std::shared_ptr<Shared> shared = _shared;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        const auto cut = shared->cuts.find(edit.sdf.ptr());
        if (cut != shared->cuts.end())
        {
            cut->second->removed = true;
            shared->cuts.erase(cut);
            return;
        }
        const auto merge = shared->merges.find(edit.sdf.ptr());
        if (merge != shared->merges.end())
        {
            merge->second->store(true);
            shared->merges.erase(merge);
            return;
        }
    }
    if (!_enabled || edit.paintOnly)
        return;

    // The edit's surroundings, as far as a piece can reach: whatever solid is
    // left in there without a path to the region's border floats.
    const float voxel = volume.get_voxel_size();
    const Bounds region = edit.bounds.expanded(float(_maxSize) * voxel);
    const glm::vec3 cells = region.get_size() / voxel;
    if (cells.x * cells.y * cells.z > float(MaxRegionCells))
        return;
    // Without normals: finding pieces needs none, and the SDF's for every cell
    // near the surface took 100+ ms (the edit waited for it). Only pieces
    // that break off get them, for their meshes.
    VoxelBlock block;
    volume.sample_region(region, block, false);
    std::vector<IslandFinder::Island> islands = IslandFinder::find(block, _maxSize);
    if (islands.empty())
        return;
    for (IslandFinder::Island &island : islands)
        volume.add_sdf_normals(*island.block);
    std::lock_guard<std::mutex> lock(shared->mutex);
    for (IslandFinder::Island &island : islands)
    {
        // A piece already on its way out (cut queued, not yet applied).
        bool pending = false;
        for (const auto &piece : shared->pieces)
            pending = pending || (!piece->removed && piece->island.block->bounds().intersects(island.block->bounds()));
        if (!pending)
            shared->found.push_back(std::move(island));
    }
}

void JarVoxelDebris::start_cut(JarVoxelVolume &volume, const IslandFinder::Island &island)
{
    auto piece = std::make_shared<Piece>();
    piece->island = island;
    piece->sdf.instantiate();
    piece->sdf->set_block(island.block);
    {
        std::lock_guard<std::mutex> lock(_shared->mutex);
        _shared->pieces.push_back(piece);
        _shared->cuts[piece->sdf.ptr()] = piece;
    }
    // Subtracting the piece's own field from the volume leaves exactly empty
    // space where it was; the materials around it stay.
    volume.edit_with(piece->sdf, SDF::SDF_OPERATION_SUBTRACTION, volume.get_global_transform(), nullptr, true);
    if (island.cells < MinCells)
    {
        piece->meshed = true; // a crumb: removed, no body
        return;
    }
    MesherSettings settings = MesherSettings::from(volume);
    settings.sdf = nullptr; // the block's cells carry their normals
    const Ref<JarBiomeMap> biomes = volume.get_biome_map();
    const UpField up = volume.get_up_field();
    JobSystem::get_pool().enqueue_background([piece, settings, biomes, up]() {
        const VoxelBlock &block = *piece->island.block;
        Array meshData;
        PackedFloat32Array painted;
        if (BlockMesher::mesh(block, settings, meshData, painted))
        {
            meshData[Mesh::ARRAY_CUSTOM1] =
                StitchedSurfaceNets::biome_weights(meshData[Mesh::ARRAY_VERTEX], biomes, up, block.origin);
            piece->meshData = meshData;
        }
        piece->meshed = true;
    });
}

void JarVoxelDebris::spawn(JarVoxelVolume &volume, Piece &piece)
{
    if (piece.meshData.is_empty())
        return; // a crumb, or no surface after all
    const VoxelBlock &block = *piece.island.block;
    const glm::vec3 offset = block.origin - piece.island.centroid; // mesh -> body space
    const Vector3 meshOffset(offset.x, offset.y, offset.z);

    Ref<ArrayMesh> mesh;
    mesh.instantiate();
    mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, piece.meshData, Array(), Dictionary(),
                                  (Mesh::ARRAY_CUSTOM_RGBA8_UNORM << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT) |
                                      (Mesh::ARRAY_CUSTOM_RGBA8_UNORM << Mesh::ARRAY_FORMAT_CUSTOM1_SHIFT));
    MeshInstance3D *meshInstance = memnew(MeshInstance3D);
    meshInstance->set_mesh(mesh);
    meshInstance->set_position(meshOffset);
    // Shaded as the terrain it was, textures fixed to the piece: its own copy
    // of the volume's material (see JarVoxelDebrisBody::pin_textures).
    Ref<ShaderMaterial> material = volume.get_runtime_material();
    if (material.is_valid())
        material = material->duplicate();
    meshInstance->set_material_override(material.is_valid() ? Ref<Material>(material) : volume.get_runtime_material());

    // A convex hull, a little inside the surface: a freshly cut piece must not
    // start out overlapping the terrain around it.
    const PackedVector3Array vertices = piece.meshData[Mesh::ARRAY_VERTEX];
    PackedVector3Array points;
    points.resize(vertices.size());
    const float inset = 0.3f * block.spacing;
    for (int64_t i = 0; i < vertices.size(); ++i)
    {
        const Vector3 p = vertices[i] + meshOffset;
        const float length = p.length();
        points.set(i, length > inset ? p * ((length - inset) / length) : Vector3());
    }
    Ref<ConvexPolygonShape3D> hull;
    hull.instantiate();
    hull->set_points(points);
    CollisionShape3D *shape = memnew(CollisionShape3D);
    shape->set_shape(hull);

    JarVoxelDebrisBody *body = memnew(JarVoxelDebrisBody);
    body->_block = piece.island.block;
    body->_centroid = piece.island.centroid;
    body->_cells = piece.island.cells;
    body->_material = material;
    body->_mesh = meshInstance;
    body->_meshOrigin = block.origin;
    body->add_child(meshInstance);
    body->add_child(shape);
    const float voxel = block.spacing;
    body->set_mass(std::max(0.1f, float(piece.island.cells) * voxel * voxel * voxel * _density));
    // On the terrain's layer, colliding with it, unless set otherwise (gravity
    // areas, e.g. a planet's, only act on the layers they watch).
    body->set_collision_layer(_collisionLayer != 0 ? _collisionLayer : volume.get_collision_layer());
    body->set_collision_mask(_collisionMask != 0 ? _collisionMask
                                                 : volume.get_collision_mask() | volume.get_collision_layer());
    // Gravity from the universe when there is one (see _integrate_forces):
    // even one tick of the engine's could launch the piece.
    const JarVoxelUniverse *universe = JarVoxelUniverse::get_singleton();
    if (universe != nullptr && universe->has_bodies())
        body->set_gravity_scale(0.0f);
    body->set_contact_monitor(true);
    body->set_max_contacts_reported(8);
    body->set_as_top_level(true); // the volume moving (an orbiting moon) does not drag it
    add_child(body);
    const glm::vec3 c = piece.island.centroid;
    body->set_global_transform(volume.get_global_transform() * Transform3D(Basis(), Vector3(c.x, c.y, c.z)));
    body->pin_textures();
    _bodies.push_back(body->get_instance_id());
    emit_signal("debris_detached", body);
}

// Main thread, every frame.
void JarVoxelDebris::process(JarVoxelVolume &volume, double delta)
{
    std::vector<IslandFinder::Island> found;
    std::vector<std::shared_ptr<Piece>> ready;
    {
        std::lock_guard<std::mutex> lock(_shared->mutex);
        found.swap(_shared->found);
        auto &pieces = _shared->pieces;
        // A cut that changed no chunk mesh (none to swap in) is on screen now.
        if (!volume.is_building() && !volume.is_meshing())
            for (const auto &piece : pieces)
                if (piece->removed)
                    piece->committed = true;
        for (auto it = pieces.begin(); it != pieces.end();)
            if ((*it)->committed && (*it)->meshed)
            {
                ready.push_back(*it);
                it = pieces.erase(it);
            }
            else
                ++it;
    }
    for (const IslandFinder::Island &island : found)
        start_cut(volume, island);
    for (const auto &piece : ready)
        spawn(volume, *piece);

    // Pieces at rest on the volume go back into it.
    int active = 0;
    JarVoxelDebrisBody *oldest = nullptr;
    for (size_t i = 0; i < _bodies.size();)
    {
        auto *body = Object::cast_to<JarVoxelDebrisBody>(ObjectDB::get_instance(_bodies[i]));
        if (body == nullptr)
        {
            _bodies.erase(_bodies.begin() + i);
            continue;
        }
        ++i;
        body->pin_textures();
        if (body->_merging)
            continue;
        active++;
        if (oldest == nullptr)
            oldest = body;
        const bool still = body->is_sleeping() ||
                           (body->get_linear_velocity().length() < 0.1f && body->get_angular_velocity().length() < 0.1f);
        const double before = body->_restTime;
        body->_restTime = still ? body->_restTime + delta : 0.0;
        if (before < _mergeDelay && body->_restTime >= _mergeDelay)
            emit_signal("debris_settled", body);
        if (_merge && body->_restTime >= _mergeDelay && rests_on_volume(body))
            merge_debris(body);
    }
    // Too many: the oldest goes back (or away).
    if (active > _maxBodies && oldest != nullptr && !oldest->_merging)
    {
        if (_merge)
            merge_debris(oldest);
        else
            remove_body(oldest);
    }
}

// Main thread: a batch of meshes (with our cuts and merges in it) is on screen.
void JarVoxelDebris::meshes_committed(JarVoxelVolume &volume)
{
    {
        std::lock_guard<std::mutex> lock(_shared->mutex);
        for (const auto &piece : _shared->pieces)
            if (piece->removed)
                piece->committed = true;
    }
    // Shown as soon as the terrain without them is (process() takes those whose
    // mesh is not ready yet).
    process(volume, 0.0);
    std::vector<JarVoxelDebrisBody *> merged;
    for (const auto &[id, merge] : _merging)
        if (merge.second->load())
            if (auto *body = Object::cast_to<JarVoxelDebrisBody>(ObjectDB::get_instance(id)))
                merged.push_back(body);
    for (JarVoxelDebrisBody *body : merged)
    {
        emit_signal("debris_merged", body);
        remove_body(body);
    }
}

bool JarVoxelDebris::rests_on_volume(JarVoxelDebrisBody *body) const
{
    const TypedArray<Node3D> touching = body->get_colliding_bodies();
    for (int64_t i = 0; i < touching.size(); ++i)
        for (Node *n = Object::cast_to<Node>(touching[i]); n != nullptr; n = n->get_parent())
            if (auto *chunk = Object::cast_to<JarVoxelChunk>(n))
            {
                if (chunk->get_volume() == _volume)
                    return true;
                break;
            }
    return false;
}

void JarVoxelDebris::merge_debris(JarVoxelDebrisBody *body)
{
    if (_volume == nullptr || body == nullptr || body->_merging || !body->_block)
        return;
    body->_merging = true;
    body->set_freeze_enabled(true); // stays put until the terrain shows it
    Ref<JarVoxelBlockSdf> sdf;
    sdf.instantiate();
    sdf->set_block(body->_block);
    auto applied = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lock(_shared->mutex);
        _shared->merges[sdf.ptr()] = applied;
    }
    _merging[body->get_instance_id()] = {sdf, applied};
    // The block's space is its volume's at the time it broke off; the body's
    // origin is the block point `centroid`.
    const glm::vec3 c = body->_centroid;
    const Transform3D blockToGlobal = body->get_global_transform() * Transform3D(Basis(), Vector3(-c.x, -c.y, -c.z));
    _volume->edit_with(sdf, SDF::SDF_OPERATION_UNION, blockToGlobal,
                       std::make_shared<VoxelBlockMaterials>(body->_block));
}

void JarVoxelDebris::remove_body(JarVoxelDebrisBody *body)
{
    const uint64_t id = body->get_instance_id();
    _bodies.erase(std::remove(_bodies.begin(), _bodies.end(), id), _bodies.end());
    _merging.erase(id);
    body->queue_free();
}

void JarVoxelDebrisBody::pin_textures()
{
    // The terrain shader takes the mesh origin's volume-space position from
    // jar_world_to_volume: keep it at where the mesh was in the volume, so the
    // piece keeps its textures however it moves.
    if (_material.is_null() || _mesh == nullptr || !_mesh->is_inside_tree())
        return;
    const Transform3D toVolume = Transform3D(Basis(), Vector3(_meshOrigin.x, _meshOrigin.y, _meshOrigin.z)) *
                                 _mesh->get_global_transform().affine_inverse();
    _material->set_shader_parameter("jar_world_to_volume", Projection(toVolume));
}

TypedArray<JarVoxelDebrisBody> JarVoxelDebris::get_bodies() const
{
    TypedArray<JarVoxelDebrisBody> result;
    for (const uint64_t id : _bodies)
        if (auto *body = Object::cast_to<JarVoxelDebrisBody>(ObjectDB::get_instance(id)))
            result.push_back(body);
    return result;
}
