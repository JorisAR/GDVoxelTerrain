#ifndef JAR_VOXEL_POPULATION_H
#define JAR_VOXEL_POPULATION_H

#include "biome_map.h"
#include "population_table.h"
#include "terrain_detail.h"
#include "terrain_feature.h"
#include "volume_extension.h"
#include <godot_cpp/classes/node.hpp>
#include <memory>
#include <vector>

using namespace godot;

class DetailScheduler;
class FeatureManager;
class JarVoxelVolume;

// Grows features and details on a JarVoxelVolume: add it as a child of one and
// give it a JarPopulationTable. Which biome is where stays the volume's
// (population_biome_map): the mesher and materials use it too. A volume
// extension: without this node nothing of it runs.
class JarVoxelPopulation : public Node, public VolumeExtension
{
    GDCLASS(JarVoxelPopulation, Node);

  public:
    // One layer of population: `item`, growing as `layer` says.
    template <typename T> struct Entry
    {
        Ref<T> item;
        PopulationLayer layer;
    };

  private:
    Ref<JarPopulationTable> _table;
    bool _featuresEnabled = true;
    bool _detailsEnabled = true;
    bool _previewFeatures = false;
    bool _previewDetails = false;
    float _featureCellSize = 64.0f;
    JarVoxelVolume *_volume = nullptr;
    std::unique_ptr<FeatureManager> _features;
    std::unique_ptr<DetailScheduler> _details;

    void attach();
    void detach();
    bool ensure_running();
    void stop();
    void reset_features();
    void reset_details();
    void on_table_changed();
    template <typename T> std::vector<Entry<T>> entries(bool enabled) const;

  public:
    JarVoxelPopulation();
    ~JarVoxelPopulation();

    Ref<JarPopulationTable> get_table() const { return _table; }
    void set_table(const Ref<JarPopulationTable> &value);
    bool get_features_enabled() const { return _featuresEnabled; }
    void set_features_enabled(bool value);
    bool get_details_enabled() const { return _detailsEnabled; }
    void set_details_enabled(bool value);
    bool get_preview_features() const { return _previewFeatures; }
    void set_preview_features(bool value);
    bool get_preview_details() const { return _previewDetails; }
    void set_preview_details(bool value);
    float get_feature_cell_size() const { return _featureCellSize; }
    void set_feature_cell_size(float value);

    void regenerate();
    void remove_features(const Vector3 &global_position, float radius);
    TypedArray<Dictionary> get_features_near(const Vector3 &global_position, float radius) const;

    // In effect now, expanded from the table: indices into these identify
    // features and details, in table order.
    std::vector<Entry<JarTerrainFeature>> feature_entries() const;
    std::vector<Entry<JarTerrainDetail>> detail_entries() const;

    // VolumeExtension
    void process(JarVoxelVolume &volume, double delta) override;
    void edit_queued(JarVoxelVolume &volume, const ModifySettings &edit, float reach) override;
    void chunk_meshed(JarVoxelVolume &volume, JarVoxelChunk *chunk) override;
    void chunk_gone(JarVoxelVolume &volume, JarVoxelChunk *chunk) override;
    void population_invalidated(JarVoxelVolume &volume) override;
    void volume_cleared(JarVoxelVolume &volume) override;
    bool is_generating(const JarVoxelVolume &volume) const override;

  protected:
    static void _bind_methods();
    void _notification(int what);
};

#endif // JAR_VOXEL_POPULATION_H
