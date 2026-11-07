#ifndef JAR_VOXEL_POPULATION_H
#define JAR_VOXEL_POPULATION_H

#include "biome_map.h"
#include "bounds.h"
#include "population_layer.h"
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

// Grows features and details on a JarVoxelVolume: add it as a child of one, and
// give it JarFeatureLayer and JarDetailLayer children, one per thing that grows,
// in the order they claim ground. How far features are drawn and where their
// impostors start follow from their size, by how large they look: settings in
// pixels, of a 1080p screen at a 75 degree field of view (the same angle
// whatever the screen, so resizing changes nothing). Which biome is where stays the volume's
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
    bool _featuresEnabled = true;
    bool _detailsEnabled = true;
    bool _previewFeatures = false;
    bool _previewDetails = false;
    float _featureCellSize = 64.0f;
    float _disappearPixels = 4.0f;
    float _impostorPixels = 24.0f;
    float _maxDrawDistance = 5000.0f;
    bool _regeneratePending = false;
    JarVoxelVolume *_volume = nullptr;
    std::unique_ptr<FeatureManager> _features;
    std::unique_ptr<FeatureManager> _farFeatures;
    std::unique_ptr<DetailScheduler> _details;
    std::vector<Bounds> _loadedRemovals; // for when the features start

    void attach();
    void detach();
    bool ensure_running();
    void stop();
    void reset_features();
    void reset_details();
    void regenerate_pending();
    TypedArray<Dictionary> features_near(const FeatureManager *manager, const Vector3 &global_position, float radius) const;
    template <typename T> std::vector<Entry<T>> entries(bool enabled) const;

  public:
    JarVoxelPopulation();
    ~JarVoxelPopulation();

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
    float get_disappear_pixels() const { return _disappearPixels; }
    void set_disappear_pixels(float value);
    float get_impostor_pixels() const { return _impostorPixels; }
    void set_impostor_pixels(float value);
    float get_max_draw_distance() const { return _maxDrawDistance; }
    void set_max_draw_distance(float value);
    // What the settings give each feature: name, impostor and draw distances.
    TypedArray<Dictionary> get_feature_distances() const;

    void regenerate();
    // A layer below changed (or came, went or moved): regenerates once, later.
    void layers_changed();
    // All layers below, in order.
    TypedArray<JarPopulationLayer> get_layers() const;
    // Script convenience: a new layer for a feature or detail, growing
    // everywhere, or in `biomes` (name -> density multiplier) if given.
    JarPopulationLayer *add_layer(const Ref<Resource> &asset, const Dictionary &biomes);
    void remove_features(const Vector3 &global_position, float radius);
    TypedArray<Dictionary> get_features_near(const Vector3 &global_position, float radius) const;
    // The same among the wide cells past the near ones (big features only).
    TypedArray<Dictionary> get_far_features_near(const Vector3 &global_position, float radius) const;

    // In effect now, expanded from the layers: indices into these identify
    // features and details, in tree order.
    std::vector<Entry<JarTerrainFeature>> feature_entries() const;
    std::vector<Entry<JarTerrainDetail>> detail_entries() const;

    // VolumeExtension
    void process(JarVoxelVolume &volume, double delta) override;
    void edit_queued(JarVoxelVolume &volume, const ModifySettings &edit, float reach) override;
    void chunk_meshed(JarVoxelVolume &volume, JarVoxelChunk *chunk) override;
    void chunk_gone(JarVoxelVolume &volume, JarVoxelChunk *chunk) override;
    void population_invalidated(JarVoxelVolume &volume) override;
    void save_state(std::vector<uint8_t> &state) override;
    void load_state(const uint8_t *data, size_t size) override;
    void volume_cleared(JarVoxelVolume &volume) override;
    bool is_generating(const JarVoxelVolume &volume) const override;

  protected:
    static void _bind_methods();
    void _notification(int what);
};

#endif // JAR_VOXEL_POPULATION_H
