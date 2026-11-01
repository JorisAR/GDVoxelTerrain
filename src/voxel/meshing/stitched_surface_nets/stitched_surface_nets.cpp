#include "stitched_surface_nets.h"
#include "material_mesh_builder.h"
#include <unordered_set>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "voxel_volume.h"
#include "utility/utils.h"
#include "qef.h"
#include <limits>

MesherSettings MesherSettings::from(const JarVoxelVolume &terrain)
{
    MesherSettings s;
    s.cubicVoxels = terrain.get_cubic_voxels();
    s.dualContouring = terrain.get_meshing_algorithm() == JarVoxelVolume::MESHING_DUAL_CONTOURING;
    s.manifold = terrain.get_manifold();
    s.featureThreshold = terrain.get_dual_contouring_feature_threshold();
    s.materialMode = static_cast<MaterialMode>(terrain.get_material_mode());
    s.sdf = terrain.get_sdf_ptr();
    s.voxelSize = terrain.get_voxel_size();
    return s;
}

StitchedSurfaceNets::StitchedSurfaceNets(const JarVoxelVolume &terrain, const VoxelOctreeNode &chunk)
    : StitchedSurfaceNets(MesherSettings::from(terrain), StitchedMeshChunk(terrain, chunk), chunk._center)
{
    _chunk = &chunk;
    if (_dualContouring && _sdf != nullptr && chunk.get_lod() <= MaxGradientLod)
    {
        _newGradients = std::make_shared<SampleGradients>();
        _newGradients->sdf = _sdf;
        _newGradients->meshVersion = terrain.get_mesh_version();
        const JarVoxelChunk *shown = chunk.get_chunk();
        if (shown != nullptr && shown->sample_gradients && shown->sample_gradients->sdf == _sdf &&
            shown->sample_gradients->meshVersion == _newGradients->meshVersion)
            _previousGradients = shown->sample_gradients;
    }
}

StitchedSurfaceNets::StitchedSurfaceNets(const MesherSettings &settings, StitchedMeshChunk &&input,
                                         const glm::vec3 &origin)
    : _origin(origin), _cubicVoxels(settings.cubicVoxels), _dualContouring(settings.dualContouring),
      _manifold(settings.manifold && !settings.cubicVoxels), _featureThreshold(settings.featureThreshold),
      _materialMode(settings.materialMode), _meshChunk(std::move(input)), _sdf(settings.sdf),
      _voxelSize(settings.voxelSize)
{
    if (_dualContouring)
    {
        _gradients.resize(_meshChunk.nodes.size());
        _distanceScale.resize(_meshChunk.nodes.size(), 1.0f);
        _hasGradient.resize(_meshChunk.nodes.size(), false);
        _projected.resize(_meshChunk.nodes.size(), 0);
        _surface.resize(_meshChunk.nodes.size());
        _surfaceNormal.resize(_meshChunk.nodes.size());
    }
}

glm::vec3 StitchedSurfaceNets::gradient_at(int node_id)
{
    if (_hasGradient[node_id])
        return _gradients[node_id];
    const VoxelOctreeNode *node = _meshChunk.nodes[node_id];
    if (node->has_normal() || _sdf == nullptr)
        _gradients[node_id] = node->has_normal() ? node->get_normal() : glm::vec3(0.0f, 1.0f, 0.0f); // edits store exact normals
    else
    {
        // The volume SDF may not be an exact distance (e.g. height minus noise
        // displacement): divide by the gradient length so the value is a true
        // distance locally. Otherwise sample planes land at the wrong height and
        // slopes come out terraced.
        // At the node's own spacing, like its value (see DetailScope).
        const SampleGradients::Entry *known =
            _previousGradients ? _previousGradients->find(node->_center, node->_size) : nullptr;
        if (known != nullptr)
        {
            _gradients[node_id] = known->gradient;
            _distanceScale[node_id] = known->distanceScale;
            if (known->projected)
            {
                _projected[node_id] = 1;
                _surface[node_id] = known->surface;
                _surfaceNormal[node_id] = known->surfaceNormal;
            }
        }
        else
        {
            const JarSignedDistanceField::DetailScope detail(_voxelSize > 0.0f ? node->edge_length(_voxelSize) : 0.0f);
            const glm::vec3 g = _sdf->gradient(node->_center);
            const float len = glm::length(g);
            _gradients[node_id] = len > 0.0f ? g / len : glm::vec3(0.0f, 1.0f, 0.0f);
            _distanceScale[node_id] = 1.0f / std::clamp(len, 0.2f, 5.0f);
        }
    }
    _hasGradient[node_id] = true;
    return _gradients[node_id];
}

// The volume SDF's surface point nearest node `node_id`'s plane (its center
// moved its distance along its normal), and the normal there; the same in
// every cell that has the node, so made once.
glm::vec3 StitchedSurfaceNets::projected_sample(int node_id, glm::vec3 &normal)
{
    if (!_projected[node_id])
    {
        VoxelOctreeNode *n = _meshChunk.nodes[node_id];
        glm::vec3 surfaceNormal = gradient_at(node_id);
        const float d = n->get_value() * _distanceScale[node_id];
        _surface[node_id] = project_to_volume_sdf(n->_center - d * surfaceNormal, surfaceNormal);
        _surfaceNormal[node_id] = surfaceNormal;
        _projected[node_id] = 1;
    }
    normal = _surfaceNormal[node_id];
    return _surface[node_id];
}

// The volume SDF's surface point nearest `point` (a few Newton steps from it),
// and the surface normal there (`normal` in and out).
glm::vec3 StitchedSurfaceNets::project_to_volume_sdf(glm::vec3 point, glm::vec3 &normal) const
{
    for (int step = 0; step < 3; ++step)
    {
        const glm::vec3 g = _sdf->gradient(point);
        const float len2 = glm::dot(g, g);
        if (len2 < 1e-8f)
            break;
        const float value = _sdf->distance(point);
        point -= value * g / len2;
        normal = g / std::sqrt(len2);
        if (std::abs(value) < 1e-3f)
            break;
    }
    return point;
}

glm::vec3 StitchedSurfaceNets::place_dual_contouring_vertex(const std::vector<int> &neighbours,
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
    // In the cell's frame (see from_cell): its corner is the origin.
    glm::vec3 base(std::numeric_limits<float>::max());
    for (int i = 0; i < 8; ++i)
        base = glm::min(base, _meshChunk.nodes[neighbours[i]]->_center);
    auto rel = [&](const VoxelOctreeNode *n) { return n->_center - base; };
    glm::vec3 cellMin(std::numeric_limits<float>::max()), cellMax(std::numeric_limits<float>::lowest());
    for (int i = 0; i < 8; ++i)
    {
        const VoxelOctreeNode *n = _meshChunk.nodes[neighbours[i]];
        cellMin = glm::min(cellMin, rel(n));
        cellMax = glm::max(cellMax, rel(n));
    }
    const float cellSize = cellMax.x - cellMin.x;
    // Where an edit meets untouched ground (edited corners carry exact normals
    // and distances, the others only the volume SDF's estimate), project the
    // untouched corners' planes onto the real surface: the estimate can be off
    // by tens of centimeters on rough terrain, and at the edit's rim that
    // flipped vertices between the edge and its two sides (jagged teeth).
    bool anyEdited = false, anyUntouched = false;
    for (int i = 0; i < 8; ++i)
        if ((cornerMask >> i) & 1)
            (_meshChunk.nodes[neighbours[i]]->has_normal() ? anyEdited : anyUntouched) = true;
    const bool editedCell = anyEdited && anyUntouched && _sdf != nullptr;
    _lastVertexSharp = false;
    SharpPlanes planes;
    Qef clusterQefs[4]; // the samples of each normal cluster (one surface each)
    glm::vec3 firstNormal(0.0f);
    float minAgreement = 1.0f; // lowest cosine between the samples' normals
    for (int i = 0; i < 8; ++i)
    {
        if (((cornerMask >> i) & 1) == 0)
            continue;
        VoxelOctreeNode *n = _meshChunk.nodes[neighbours[i]];
        glm::vec3 normal = gradient_at(neighbours[i]);
        const float d = n->get_value() * _distanceScale[neighbours[i]];
        if (std::abs(d) > 1.5f * cellSize)
            continue; // too far for its closest feature to be in this cell
        glm::vec3 point = rel(n) - d * normal;
        if (editedCell && !n->has_normal())
            point = projected_sample(neighbours[i], normal) - base; // the SDF is in volume space
        if (qef.count() == 0)
            firstNormal = normal;
        minAgreement = std::min(minAgreement, glm::dot(firstNormal, normal));
        qef.add(point, normal);
        averageNormal += normal;
        // Cluster the sample normals (within ~25 degrees), in corner order.
        int cluster = -1;
        for (int c = 0; c < planes.count && cluster < 0; ++c)
            if (glm::dot(glm::normalize(planes.normals[c]), normal) >= 0.9f)
                cluster = c;
        if (cluster < 0 && planes.count < 4)
            planes.normals[cluster = planes.count++] = glm::vec3(0.0f);
        if (cluster >= 0)
        {
            planes.normals[cluster] += normal;
            clusterQefs[cluster].add(point, normal);
        }
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
        crossings += glm::mix(rel(na), rel(nb), std::abs(va) / (std::abs(va) + std::abs(vb)));
        crossingCount++;
    }
    const glm::vec3 target = crossingCount > 0 ? crossings / float(crossingCount) : qef.mass_point();

    // Try the full feature first (corner), then simpler ones (edge, plane):
    // a corner or edge outside the cell belongs to a neighbouring cell, and
    // this cell's vertex then lies on the remaining edge or face.
    const glm::vec3 tolerance(cellSize * 0.01f);
    for (int rank = qef.rank(_featureThreshold); rank >= 1; --rank)
    {
        if (rank == 1 && planes.count >= 2)
        {
            // Two surfaces meet near this cell, but not inside it (no edge or
            // corner solution fits). One plane averaged from both runs through
            // the solid between them: a vertex there juts out of the mesh as a
            // tooth along the edge. The cell holds one of the surfaces: put the
            // vertex on the one that fits closest to where the surface crosses.
            float best = std::numeric_limits<float>::max();
            glm::vec3 bestPoint(0.0f);
            for (int c = 0; c < planes.count; ++c)
            {
                Qef::Solution candidate = clusterQefs[c].solve_rank(1, _featureThreshold);
                if (!Qef::fit_into(candidate, target, cellMin - tolerance, cellMax + tolerance))
                    continue;
                const float distance = glm::distance(candidate.point, target);
                if (distance < best)
                {
                    best = distance;
                    bestPoint = candidate.point;
                }
            }
            if (best < std::numeric_limits<float>::max())
                return from_cell(glm::clamp(bestPoint, cellMin, cellMax), base);
        }
        Qef::Solution solution = qef.solve_rank(rank, _featureThreshold);
        if (Qef::fit_into(solution, target, cellMin - tolerance, cellMax + tolerance))
        {
            // An edge or corner solution where the normals really disagree is a
            // sharp feature (not just curvature).
            constexpr float SharpAgreement = 0.8f; // ~37 degrees
            _lastVertexSharp = rank >= 2 && minAgreement < SharpAgreement;
            _lastPlanes = SharpPlanes();
            if (_lastVertexSharp)
            {
                _lastPlanes = planes;
                for (int c = 0; c < planes.count; ++c)
                    _lastPlanes.normals[c] = glm::normalize(planes.normals[c]);
            }
            return from_cell(glm::clamp(solution.point, cellMin, cellMax), base);
        }
    }
    return glm::vec3(std::numeric_limits<float>::quiet_NaN());
}

void StitchedSurfaceNets::create_vertex(const int cell, const std::vector<int> &neighbours)
{
    glm::vec3 vertexPosition(0.0f);
    MaterialAccumulator materials;
    glm::vec3 normal(0.0f);
    int edge_crossings = 0;
    glm::vec3 base(std::numeric_limits<float>::max()); // the cell's frame (see from_cell)
    for (int i = 0; i < 8; ++i)
        base = glm::min(base, _meshChunk.nodes[neighbours[i]]->_center);
    for (auto &edge : StitchedMeshChunk::Edges)
    {
        auto na = _meshChunk.nodes[neighbours[edge.x]];
        auto nb = _meshChunk.nodes[neighbours[edge.y]];
        float valueA = na->get_value();
        float valueB = nb->get_value();
        glm::vec3 posA = na->_center - base;
        glm::vec3 posB = nb->_center - base;

        normal += (valueB - valueA) * (posB - posA);

        if (glm::sign(valueA) == glm::sign(valueB))
            continue;

        float t = glm::abs(valueA) / (glm::abs(valueA) + glm::abs(valueB));
        vertexPosition += glm::mix(posA, posB, t);
        edge_crossings++;
        materials.add(na->get_materials(), 1.0f - t);
        materials.add(nb->get_materials(), t);
    }

    if (edge_crossings <= 0)
        return;

    if (_manifold)
    {
        create_component_vertices(cell, neighbours);
        return;
    }

    vertexPosition = from_cell(vertexPosition / static_cast<float>(edge_crossings), base);
    materials.scale(1.0f / static_cast<float>(edge_crossings));
    normal = glm::normalize(normal);
    if (_cubicVoxels)
        vertexPosition = local(_meshChunk.nodes[neighbours[0]]);
    else if (_dualContouring)
    {
        // A vertex depends only on its cell's samples: every chunk that has
        // the cell places it alike (no cracks between chunks or LODs).
        glm::vec3 exactNormal(0.0f);
        const glm::vec3 dc = place_dual_contouring_vertex(neighbours, exactNormal);
        if (!glm::any(glm::isnan(dc)))
            vertexPosition = dc;
        if (glm::dot(exactNormal, exactNormal) > 0.5f)
            normal = exactNormal; // exact surface normal instead of the value-difference estimate
    }

    _cells[cell].vertex = _verts.size();
    _sharp.push_back(_dualContouring && _lastVertexSharp ? 1 : 0);
    _sharpPlanes.push_back(_dualContouring && _lastVertexSharp ? _lastPlanes : SharpPlanes());
    _verts.push_back({vertexPosition.x, vertexPosition.y, vertexPosition.z});
    _normals.push_back({normal.x, normal.y, normal.z});
    _vertexMaterials.push_back(VertexMaterials::from(materials));
}

int StitchedSurfaceNets::cell_vertex(const glm::ivec3 &point, int &cell)
{
    const glm::ivec3 points = _meshChunk.dims + 1;
    if (glm::any(glm::lessThan(point, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(point, points)))
        return -1;
    int &slot = _cellAt[point.x + points.x * (point.y + points.y * point.z)];
    if (slot == -1)
    {
        // The samples in the 8 octants around the point.
        thread_local std::vector<int> corners;
        corners.resize(8);
        slot = -2;
        for (int o = 0; o < 8; ++o)
        {
            const glm::ivec3 c = point - glm::ivec3((o & 1) ? 0 : 1, (o & 2) ? 0 : 1, (o & 4) ? 0 : 1);
            corners[o] = _meshChunk.sample_at(c);
            if (corners[o] < 0)
                return -1; // at the edge of what this chunk has: never needed there
        }
        slot = int(_cells.size());
        Cell made;
        std::copy(corners.begin(), corners.end(), made.corners);
        _cells.push_back(made);
        if (_manifold)
        {
            std::array<uint8_t, 12> none;
            none.fill(NoComponent);
            _edgeComponent.push_back(none);
        }
        create_vertex(slot, corners);
    }
    if (slot < 0)
        return -1;
    cell = slot;
    return _cells[slot].vertex;
}

void StitchedSurfaceNets::face_quad(int negative, int positive, int axis, const glm::ivec3 &minCorner, int size)
{
    // The face's other two axes in order (u, v). Its outline, in the cyclic
    // order (lo, lo), (hi, lo), (hi, hi), (lo, hi), with the octree vertices
    // between its corners.
    const int u = axis == 0 ? 1 : 0;
    const int v = axis == 2 ? 1 : 2;
    glm::ivec3 corner[4] = {minCorner, minCorner, minCorner, minCorner};
    corner[1][u] += size;
    corner[2][u] += size;
    corner[2][v] += size;
    corner[3][v] += size;
    thread_local std::vector<glm::ivec3> outline;
    outline.clear();
    int cornerAt[4];
    for (int c = 0; c < 4; ++c)
    {
        cornerAt[c] = int(outline.size());
        outline.push_back(corner[c]);
        const glm::ivec3 step = glm::sign(corner[(c + 1) % 4] - corner[c]);
        for (int k = 1; k < size; ++k)
            if (_meshChunk.is_corner(corner[c] + step * k))
                outline.push_back(corner[c] + step * k);
    }
    // Twice the face's center, in lattice units (integers).
    const glm::ivec3 center2 = 2 * minCorner + size;
    thread_local std::vector<int> n;
    n.resize(outline.size());
    for (size_t k = 0; k < outline.size(); ++k)
    {
        const glm::ivec3 point = outline[k];
        int cell = -1;
        n[k] = cell_vertex(point, cell);
        if (n[k] < 0)
            return;
        if (_manifold)
        {
            // The cell's edge between the face's two samples: from the
            // octant on the negative side towards the face's center.
            const int toward = (2 * point[u] <= center2[u] ? 1 << u : 0) | (2 * point[v] <= center2[v] ? 1 << v : 0);
            const int edge = axis == 0 ? toward / 2 : (axis == 1 ? 4 + (toward & 1) + 2 * ((toward >> 2) & 1) : 8 + toward);
            const uint8_t component = _edgeComponent[cell][edge];
            if (component == NoComponent)
                return;
            n[k] += component;
        }
    }
    // Wound from the solid sample to the empty one. The (u, v) corner order
    // runs the other way around the y axis (x, z) than around x (y, z) and
    // z (x, y): there it flips for the other sample.
    const bool flip = _meshChunk.nodes[axis == 1 ? positive : negative]->get_value() < 0.0f;
    if (outline.size() == 4)
    {
        add_quad(n[cornerAt[0]], n[cornerAt[1]], n[cornerAt[3]], n[cornerAt[2]], flip, positive, negative);
        return;
    }
    // More than 4: fanned around the face's surface crossing (as add_quad
    // does with a folded quad, in the same winding).
    const int c = add_crossing_vertex(positive, negative);
    for (size_t k = 0; k < n.size(); ++k)
        add_tri(n[k], n[(k + 1) % n.size()], c, flip);
}

ChunkMeshData *StitchedSurfaceNets::generate_mesh_data(const JarVoxelVolume &terrain)
{
    Array meshData;
    PackedFloat32Array painted;
    if (!generate_arrays(meshData, painted))
        return nullptr;
    meshData[Mesh::ARRAY_CUSTOM1] = biome_weights(meshData[Mesh::ARRAY_VERTEX], terrain.get_biome_map(),
                                                  terrain.get_up_field(), _origin);
    ChunkMeshData *output = new ChunkMeshData(meshData, _chunk->get_lod(), _meshChunk.is_edge_chunk(),
                                              _chunk->get_bounds(terrain.get_voxel_size()));
    output->boundaries = _meshChunk.boundaries;
    output->mesh_version = terrain.get_mesh_version();
    output->painted = painted;
    if (_newGradients)
    {
        for (int i = 0; i < int(_meshChunk.nodes.size()); ++i)
            if (_hasGradient[i] && !_meshChunk.nodes[i]->has_normal())
                _newGradients->entries.push_back({_meshChunk.nodes[i]->_center, _meshChunk.nodes[i]->_size,
                                                  _gradients[i], _distanceScale[i], _projected[i] != 0, _surface[i],
                                                  _surfaceNormal[i]});
        std::sort(_newGradients->entries.begin(), _newGradients->entries.end());
        output->gradients = std::move(_newGradients);
    }
    return output;
}

PackedByteArray StitchedSurfaceNets::biome_weights(const PackedVector3Array &verts, const Ref<JarBiomeMap> &biomes,
                                                   const UpField &up, const glm::vec3 &origin)
{
    PackedByteArray custom1;
    custom1.resize(verts.size() * 4);
    uint8_t *w = custom1.ptrw();
    for (int64_t v = 0; v < verts.size(); ++v)
    {
        float weights[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        if (biomes.is_valid() && biomes->biome_count() > 1)
        {
            const Vector3 p = verts[v];
            const BiomeWeights b = biomes->evaluate(glm::vec3(p.x, p.y, p.z) + origin, up);
            weights[0] = 0.0f;
            for (int i = 0; i < b.count; ++i)
                if (b.ids[i] < 4)
                    weights[b.ids[i]] += b.weights[i];
        }
        for (int i = 0; i < 4; ++i)
            w[4 * v + i] = uint8_t(std::clamp(weights[i], 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    return custom1;
}

bool StitchedSurfaceNets::generate_arrays(Array &meshData, PackedFloat32Array &painted)
{
    // Each of this chunk's own samples meshes its faces: those towards equal
    // samples on its positive sides, and all towards larger ones (smaller
    // ones mesh their own).
    _cellAt.assign(size_t(_meshChunk.dims.x + 1) * (_meshChunk.dims.y + 1) * (_meshChunk.dims.z + 1), -1);
    for (int i = 0; i < int(_meshChunk.nodes.size()); ++i)
    {
        if (!_meshChunk.own[i])
            continue;
        const glm::ivec3 m = _meshChunk.minCell[i];
        const int size = _meshChunk.cells[i];
        const bool inside = _meshChunk.nodes[i]->get_value() < 0.0f;
        for (int axis = 0; axis < 3; ++axis)
            for (int dir = -1; dir <= 1; dir += 2)
            {
                glm::ivec3 across = m;
                across[axis] = dir > 0 ? m[axis] + size : m[axis] - 1;
                const int j = _meshChunk.sample_at(across);
                if (j < 0)
                    continue;
                const int other = _meshChunk.cells[j];
                if (other < size || (other == size && dir < 0))
                    continue;
                if ((_meshChunk.nodes[j]->get_value() < 0.0f) == inside)
                    continue; // no surface between them
                glm::ivec3 corner = m;
                corner[axis] = dir > 0 ? m[axis] + size : m[axis];
                if (dir > 0)
                    face_quad(i, j, axis, corner, size);
                else
                    face_quad(j, i, axis, corner, size);
            }
    }

    if (_indices.size() == 0)
        return false;

    if (_dualContouring && !_cubicVoxels)
        split_creases();

    meshData.resize(Mesh::ARRAY_MAX);
    PackedColorArray colors;
    PackedByteArray custom0;
    MaterialMeshBuilder::build(_materialMode, _verts, _normals, _indices, _vertexMaterials, colors, custom0, painted);
    meshData[Mesh::ARRAY_VERTEX] = _verts; // the builder may have split vertices
    meshData[Mesh::ARRAY_NORMAL] = _normals;
    meshData[Mesh::ARRAY_COLOR] = colors;
    meshData[Mesh::ARRAY_CUSTOM0] = custom0;
    meshData[Mesh::ARRAY_INDEX] = _indices;
    return true;
}

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
void StitchedSurfaceNets::create_component_vertices(const int cell, const std::vector<int> &neighbours)
{
    std::array<uint8_t, 12> &edgeComponent = _edgeComponent[cell];
    const int components = find_components(neighbours, edgeComponent);
    if (components > 0)
        _cells[cell].vertex = _verts.size();
    for (int c = 0; c < components; ++c)
    {
        uint16_t edgeMask = 0;
        glm::vec3 position(0.0f), normal(0.0f);
        MaterialAccumulator materials;
        int crossings = 0;
        glm::vec3 base(std::numeric_limits<float>::max()); // the cell's frame (see from_cell)
        for (int i = 0; i < 8; ++i)
            base = glm::min(base, _meshChunk.nodes[neighbours[i]]->_center);
        for (int e = 0; e < 12; ++e)
        {
            if (edgeComponent[e] != c)
                continue;
            edgeMask |= uint16_t(1 << e);
            VoxelOctreeNode *na = _meshChunk.nodes[neighbours[StitchedMeshChunk::Edges[e].x]];
            VoxelOctreeNode *nb = _meshChunk.nodes[neighbours[StitchedMeshChunk::Edges[e].y]];
            const float va = na->get_value(), vb = nb->get_value();
            const float t = std::abs(va) / (std::abs(va) + std::abs(vb));
            position += glm::mix(na->_center - base, nb->_center - base, t);
            materials.add(na->get_materials(), 1.0f - t);
            materials.add(nb->get_materials(), t);
            normal += (vb - va) * (nb->_center - na->_center);
            crossings++;
        }
        position = from_cell(position / float(crossings), base);
        materials.scale(1.0f / float(crossings));
        normal = glm::length(normal) > 0.0f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
        if (_dualContouring)
        {
            glm::vec3 exactNormal(0.0f);
            const glm::vec3 dc = place_dual_contouring_vertex(neighbours, exactNormal, edgeMask);
            if (!glm::any(glm::isnan(dc)))
                position = dc;
            if (glm::dot(exactNormal, exactNormal) > 0.5f)
                normal = exactNormal;
        }
        _sharp.push_back(_dualContouring && _lastVertexSharp ? 1 : 0);
        _sharpPlanes.push_back(_dualContouring && _lastVertexSharp ? _lastPlanes : SharpPlanes());
        _verts.push_back({position.x, position.y, position.z});
        _normals.push_back({normal.x, normal.y, normal.z});
        _vertexMaterials.push_back(VertexMaterials::from(materials));
    }
}

// Quad n0 n1 / n2 n3 (n0-n3 and n1-n2 are the diagonals).
void StitchedSurfaceNets::add_quad(int n0, int n1, int n2, int n3, bool flip, int a, int b)
{
    // Outward: from the solid sample to the empty one.
    VoxelOctreeNode *na = _meshChunk.nodes[a];
    VoxelOctreeNode *nb = _meshChunk.nodes[b];
    const glm::vec3 edge = na->get_value() < nb->get_value() ? nb->_center - na->_center : na->_center - nb->_center;
    const Vector3 outward = Vector3(edge.x, edge.y, edge.z).normalized();
    // How well triangle (i, j, k), as add_tri emits it, faces outward (cosine).
    auto facing = [&](int i, int j, int k) {
        if (flip)
            std::swap(i, j);
        // Godot's front faces are wound clockwise.
        const Vector3 n = (_verts[k] - _verts[i]).cross(_verts[j] - _verts[i]);
        const real_t len = n.length();
        return len <= 1e-12f ? real_t(0) : n.dot(outward) / len;
    };
    const real_t facing03 = std::min(facing(n0, n1, n3), facing(n0, n3, n2));
    const real_t facing12 = std::min(facing(n1, n3, n2), facing(n1, n2, n0));
    constexpr real_t Folded = 0.05f;
    if (facing03 <= Folded && facing12 <= Folded)
    {
        const int c = add_crossing_vertex(a, b);
        add_tri(n0, n1, c, flip);
        add_tri(n1, n3, c, flip);
        add_tri(n3, n2, c, flip);
        add_tri(n2, n0, c, flip);
        return;
    }
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
    // Never a folded split when the other one is not.
    if (facing03 <= Folded)
        splitAlong03 = false;
    else if (facing12 <= Folded)
        splitAlong03 = true;
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

// A vertex where the surface crosses the edge between samples `a` and `b`
// (depends only on the two samples, so chunks sharing the edge agree).
int StitchedSurfaceNets::add_crossing_vertex(int a, int b)
{
    VoxelOctreeNode *na = _meshChunk.nodes[a];
    VoxelOctreeNode *nb = _meshChunk.nodes[b];
    const float va = na->get_value(), vb = nb->get_value();
    const float t = std::abs(va) / std::max(std::abs(va) + std::abs(vb), 1e-12f);
    const glm::vec3 base = cell_base(na, nb); // the edge's frame (see from_cell)
    const glm::vec3 position = from_cell(glm::mix(na->_center - base, nb->_center - base, t), base);
    glm::vec3 normal = (vb - va) * (nb->_center - na->_center);
    if (_dualContouring)
        normal = glm::mix(gradient_at(a), gradient_at(b), t);
    normal = glm::length(normal) > 0.0f ? glm::normalize(normal) : glm::vec3(0.0f, 1.0f, 0.0f);
    MaterialAccumulator materials;
    materials.add(na->get_materials(), 1.0f - t);
    materials.add(nb->get_materials(), t);
    const int index = int(_verts.size());
    _sharp.push_back(0);
    _sharpPlanes.push_back(SharpPlanes());
    _verts.push_back({position.x, position.y, position.z});
    _normals.push_back({normal.x, normal.y, normal.z});
    _vertexMaterials.push_back(VertexMaterials::from(materials));
    return index;
}

// Smooth normals within a surface, but not across sharp creases: each triangle
// corner averages only the neighbouring faces within CreaseAngle of its own
// face, and vertices whose corners disagree are duplicated.
//
// Vertices on smooth surface keep the normal from their samples for the faces
// that agree with it: that normal depends only on the vertex's cell, so chunks
// sharing a border vertex shade it identically (face averages would differ, as
// each chunk only sees its own faces: visible seams). Only faces folded away
// from it, and sharp features, get averaged normals on copies.
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
        const bool keepSample = !_sharp[v];
        const Vector3 sample = _normals[v];
        if (keepSample)
            variants.emplace_back(sample, int(v));
        const SharpPlanes &planes = _sharpPlanes[v];
        for (int t : tris)
        {
            const Vector3 own = faceNormals[t].normalized();
            if (keepSample && own.dot(sample) >= SmoothCreaseCos)
                continue; // stays on the vertex, with its sample normal
            Vector3 n;
            if (planes.count >= 2)
            {
                // The surface direction of the cell closest to this face.
                float best = -2.0f;
                for (int c = 0; c < planes.count; ++c)
                {
                    const Vector3 p(planes.normals[c].x, planes.normals[c].y, planes.normals[c].z);
                    if (p.dot(own) > best)
                    {
                        best = p.dot(own);
                        n = p;
                    }
                }
            }
            else
            {
                Vector3 sum;
                for (int o : tris)
                    if (faceNormals[o].normalized().dot(own) >= creaseCos)
                        sum += faceNormals[o];
                n = sum.length_squared() > 0.0f ? sum.normalized() : own;
            }

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
                    _vertexMaterials.push_back(_vertexMaterials[v]);
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