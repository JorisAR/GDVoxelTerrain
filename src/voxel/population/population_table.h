#ifndef JAR_POPULATION_TABLE_H
#define JAR_POPULATION_TABLE_H

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/typed_array.hpp>

using namespace godot;

// What grows where: one row per JarTerrainFeature or JarTerrainDetail, with
// the biomes it grows in. A row is a dictionary, sparse and readable as text:
//   {"group": "Trees", "asset": <resource>, "biomes": {"Tundra": 0.3, "*": 1.0}}
// `biomes` maps a biome's resource_name to a density multiplier; "*" is every
// other biome (alone: everywhere, also without biomes), and 0 leaves one out.
// Rows are layers: earlier features claim their footprint first.
class JarPopulationTable : public Resource
{
    GDCLASS(JarPopulationTable, Resource);

  private:
    TypedArray<Dictionary> rows;

    static Dictionary everywhere()
    {
        Dictionary result;
        result["*"] = 1.0;
        return result;
    }

  public:
    TypedArray<Dictionary> get_rows() const { return rows; }
    void set_rows(const TypedArray<Dictionary> &v) { rows = v; emit_changed(); }
    void add(const Ref<Resource> &asset, const Dictionary &biomes, const String &group)
    {
        Dictionary row;
        row["group"] = group;
        row["asset"] = asset;
        row["biomes"] = biomes.is_empty() ? everywhere() : biomes;
        rows.push_back(row);
        emit_changed();
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("get_rows"), &JarPopulationTable::get_rows);
        ClassDB::bind_method(D_METHOD("set_rows", "value"), &JarPopulationTable::set_rows);
        ClassDB::bind_method(D_METHOD("add", "asset", "biomes", "group"), &JarPopulationTable::add,
                             DEFVAL(Dictionary()), DEFVAL(String()));
        ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "rows", PROPERTY_HINT_ARRAY_TYPE, "Dictionary"), "set_rows", "get_rows");
    }
};

#endif // JAR_POPULATION_TABLE_H
