#ifndef JAR_FEATURE_MANAGER_H
#define JAR_FEATURE_MANAGER_H

#include "bounds.h"
#include "signed_distance_field.h"
#include "terrain_feature.h"
#include "biome_map.h"
#include "up_field.h"
#include <atomic>
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
// Scans and generation run on the shared worker pool; nodes are created on the
// main thread within a time budget.
class FeatureManager
{
  public:
    struct Instance
    {
        int feature;
        Transform3D transform; // volume space
    };

    explicit FeatureManager(JarVoxelVolume &volume);
    ~FeatureManager(); // waits for running jobs

    void process(); // main thread, every frame
    // Main thread: features with their base inside `bounds` (volume space) are
    // removed now and never generated again.
    void remove_in(const Bounds &bounds);
    // Main thread: drops everything (features or settings changed).
    void reset();
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
    struct Cell
    {
        bool ready = false;
        std::vector<std::vector<Transform3D>> perFeature; // volume space
        std::vector<Node3D *> groups;                     // one node per feature, null when hidden
    };

    JarVoxelVolume &_volume;
    std::shared_ptr<Shared> _shared;
    std::unordered_map<glm::ivec3, Cell> _cells;
    std::vector<Bounds> _removed; // edits and explicit removals, volume space
    std::vector<Ref<JarTerrainFeature>> _features;
    std::vector<int> _featureBiomes; // per feature: biome index, -1 = everywhere
    Ref<JarBiomeMap> _biomeMap;
    float _cellSize = 64.0f;
    float _range = 0.0f; // largest draw distance
    std::vector<glm::vec3> _scanViewers;
    bool _scanRunning = false;
    bool _dirtyVisibility = false;
    std::vector<glm::ivec3> _toShow; // cells whose groups need (re)building

    void refresh_features();
    void start_scan(const std::vector<glm::vec3> &viewers);
    void start_generation(const glm::ivec3 &key);
    void apply_results();
    void update_visibility(const std::vector<glm::vec3> &viewers);
    void build_groups(const glm::ivec3 &key, Cell &cell, const std::vector<glm::vec3> &viewers);
    void free_groups(Cell &cell);
    bool is_removed(const glm::vec3 &position) const;
    Bounds cell_bounds(const glm::ivec3 &key) const;

    static std::vector<Instance> generate_cell(const glm::ivec3 &key, float cellSize,
                                               const std::vector<Ref<JarTerrainFeature>> &features,
                                               const std::vector<int> &featureBiomes, const Ref<JarBiomeMap> &biomes,
                                               const Ref<JarSignedDistanceField> &sdf, const UpField &up);
    static std::vector<glm::ivec3> scan_cells(const std::vector<glm::vec3> &viewers, float range, float cellSize,
                                              const Ref<JarSignedDistanceField> &sdf);
};

#endif // JAR_FEATURE_MANAGER_H
