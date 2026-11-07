#ifndef JAR_FEATURE_MANAGER_H
#define JAR_FEATURE_MANAGER_H

#include "bounds.h"
#include "modify_settings.h"
#include "signed_distance_field.h"
#include "terrain_feature.h"
#include "biome_map.h"
#include "ground_palette.h"
#include "up_field.h"
#include <godot_cpp/classes/shape3d.hpp>
#include <atomic>
#include <deque>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/typed_array.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

using namespace godot;

class JarVoxelPopulation;
class JarVoxelVolume;

// Places JarTerrainFeatures on a volume's SDF surface (after the population
// approach in https://jorisar.nl/blog/Population/).
//
// Space is divided into cubic cells. Only cells that the surface passes
// through and that lie within the features' draw distance of a viewer exist
// (a sparse version of a feature octree's leaf level). For each cell, Poisson
// disc samples are taken on a square above it (facing the volume's up), ray
// marched down to the SDF surface, and kept if the hit lies inside the cell.
// Each sample picks a feature by density, then the feature's rules decide.
// Generation is deterministic per cell, so features stand in the same place
// every time; edits and remove_features() record their bounds and features
// inside them never come back.
//
// Two managers work together (see JarVoxelPopulation). Cells up to NearRange
// are a few dozen meters wide. Beyond it, big features are drawn from cells
// FarSplit times wider, or the cells would number in the tens of thousands; a
// far cell generates the near cells inside it, so a feature crossing from one
// to the other stays exactly where it stands.
//
// Scans and generation run on the shared worker pool (low priority); nodes are
// created on the main thread within a time budget. A feature's scene is read
// once into a template (see JarTerrainFeature): its meshes become one
// multimesh per cell, its collision shapes one static body per cell near
// viewers.
class FeatureManager
{
  public:
    struct Instance
    {
        int feature;
        Transform3D transform; // volume space
        glm::vec3 ground;      // linear color of the ground there
    };

    static constexpr float NearRange = 1024.0f; // meters
    static constexpr int FarSplit = 8;          // near cells across a far cell
    // Radians one pixel covers: 1080 pixels over a 75 degree field of view.
    static constexpr float PixelAngle = 0.001421f;

    FeatureManager(JarVoxelVolume &volume, const JarVoxelPopulation &population, bool far);
    // Where a feature gets its impostor (0: never) and how far it is drawn.
    struct Distances
    {
        float impostor = 0.0f, draw = 0.0f;
    };
    static Distances distances_of(const Ref<JarTerrainFeature> &feature, const JarVoxelPopulation &population);
    ~FeatureManager(); // waits for running jobs

    void process(); // main thread, every frame
    // Main thread: features with their base inside `bounds` (volume space) are
    // removed now and never generated again.
    void remove_in(const Bounds &bounds);
    // Features whose ground an edit changes: those where the edit's shape
    // reaches their base (or buries their lower meter), within `reach`.
    void remove_edited(const ModifySettings &edit, float reach);
    // Main thread: drops everything (features or settings changed).
    void reset();
    // Bounds of what remove_in removed, for a save.
    std::vector<Bounds> removed_bounds() const;
    std::vector<Instance> instances_near(const glm::vec3 &position, float radius) const;
    int cell_count() const { return int(_cells.size()); }
    Ref<JarTerrainFeature> feature_at(int index) const
    {
        return index >= 0 && index < int(_features.size()) ? _features[index] : Ref<JarTerrainFeature>();
    }

  private:
    struct Shared
    {
        std::mutex mutex;
        std::vector<std::pair<glm::ivec3, std::vector<Instance>>> generated;
        std::vector<glm::ivec3> scanned;
        bool scanDone = false;
        std::atomic<int> activeJobs{0};
    };
    struct Placed
    {
        Transform3D transform; // volume space
        glm::vec3 ground;
    };
    struct Cell
    {
        bool started = false; // its generation job was started
        bool ready = false;
        std::vector<std::vector<Placed>> perFeature;
        std::vector<Node3D *> groups;    // one node per feature, null when hidden
        std::vector<Node3D *> colliders; // one body per feature, null when far
    };
    // What a feature draws and collides with, relative to its transform.
    struct Template
    {
        struct Part
        {
            Ref<Mesh> mesh;
            Ref<Material> material; // override, may be null
            Transform3D local;
            float begin = 0.0f, end = 0.0f; // visibility range, end 0 = no limit
            GeometryInstance3D::ShadowCastingSetting shadows = GeometryInstance3D::SHADOW_CASTING_SETTING_ON;
        };
        struct Collider
        {
            Ref<Shape3D> shape;
            Transform3D local;
        };
        std::vector<Part> parts;
        std::vector<Collider> colliders;
        uint32_t layer = 1, mask = 1;
        bool nodes = false; // instantiate the scene per feature instead
    };

    // Only the running scan uses it (one at a time).
    struct ScanCache
    {
        std::unordered_map<glm::ivec3, bool> blocks, cells;
    };
    std::shared_ptr<ScanCache> _scanCache = std::make_shared<ScanCache>();

    JarVoxelVolume &_volume;
    const JarVoxelPopulation &_population;
    std::shared_ptr<Shared> _shared;
    std::unordered_map<glm::ivec3, Cell> _cells;
    // Edits and explicit removals (volume space). An edit removes only what it
    // touches (see Removal::hits), also from cells generated later.
    struct Removal
    {
        Bounds bounds;
        std::shared_ptr<const ModifySettings> edit; // null: everything in `bounds`
        float reach = 0.0f;
        bool hits(const Transform3D &feature) const;
    };
    std::vector<Removal> _removed;
    void apply_removal(const Removal &removal);
    std::vector<Ref<JarTerrainFeature>> _features;
    std::vector<PopulationLayer> _layers; // per feature
    std::vector<float> _draws;            // per feature: how far this manager draws it, 0: not at all
    bool _far = false;
    float _begin = 0.0f; // where this manager takes over
    std::vector<Template> _templates; // per feature
    Ref<JarBiomeMap> _biomeMap;
    float _cellSize = 64.0f;
    float _range = 0.0f; // how far anything is drawn
    std::vector<glm::vec3> _scanViewers;
    bool _scanRunning = false;
    bool _dirtyVisibility = false;
    std::vector<glm::ivec3> _toShow; // cells whose groups need (re)building
    std::deque<glm::ivec3> _toGenerate; // nearest first

    void refresh_features();
    void start_scan(const std::vector<glm::vec3> &viewers);
    void start_generation(const glm::ivec3 &key);
    void apply_results();
    void update_visibility(const std::vector<glm::vec3> &viewers);
    void build_groups(const glm::ivec3 &key, Cell &cell, const std::vector<glm::vec3> &viewers);
    Node3D *build_colliders(const Template &t, const std::vector<Placed> &placed) const;
    static Template make_template(const Ref<JarTerrainFeature> &feature);
    static void add_impostor(Template &t, const JarTerrainFeature &feature, float distance);
    static float size_of(const Template &t, const JarTerrainFeature &feature);
    static Distances distances_of(const Template &t, const Ref<JarTerrainFeature> &feature,
                                  const JarVoxelPopulation &population);
    void free_groups(Cell &cell);
    bool is_removed(const glm::vec3 &position) const;
    Bounds cell_bounds(const glm::ivec3 &key) const;

    static std::vector<Instance> generate_cell(const glm::ivec3 &key, float cellSize,
                                               const std::vector<Ref<JarTerrainFeature>> &features,
                                               const std::vector<PopulationLayer> &layers, const Ref<JarBiomeMap> &biomes,
                                               const Ref<JarSignedDistanceField> &sdf, const UpField &up,
                                               const std::shared_ptr<const GroundPalette> &ground);
    static std::vector<glm::ivec3> scan_cells(const std::vector<glm::vec3> &viewers, float range, float cellSize,
                                              float closest, const Ref<JarSignedDistanceField> &sdf, ScanCache &cache);
};

#endif // JAR_FEATURE_MANAGER_H
