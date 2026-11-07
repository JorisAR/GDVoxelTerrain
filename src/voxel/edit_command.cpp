#include "edit_command.h"
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/variant/dictionary.hpp>

namespace
{
constexpr int MaxDepth = 16; // brushes nest; data from elsewhere should not recurse forever

bool may_be_made(const String &name)
{
    ClassDBSingleton *classes = ClassDBSingleton::get_singleton();
    if (!classes->class_exists(name) || !classes->can_instantiate(name))
        return false;
    for (const char *allowed : {"JarSignedDistanceField", "JarNoise", "Noise"})
        if (name == allowed || classes->is_parent_class(name, allowed))
            return true;
    return false;
}

bool is_file(const String &path)
{
    return path.begins_with("res://") && !path.contains("::");
}

Variant encode_value(const Variant &value, int depth, bool &ok);

Variant encode_resource(const Ref<Resource> &resource, int depth, bool &ok)
{
    Dictionary result;
    if (resource.is_null())
        return Variant();
    if (is_file(resource->get_path()))
    {
        result["path"] = resource->get_path();
        return result;
    }
    if (depth >= MaxDepth || !may_be_made(resource->get_class()))
    {
        ok = false;
        return Variant();
    }
    result["class"] = resource->get_class();
    const TypedArray<Dictionary> properties = resource->get_property_list();
    for (int64_t i = 0; i < properties.size(); ++i)
    {
        const Dictionary property = properties[i];
        const String name = property["name"];
        if ((int(property["usage"]) & PROPERTY_USAGE_STORAGE) == 0 || name == "script" || name.begins_with("resource_"))
            continue;
        result[name] = encode_value(resource->get(name), depth + 1, ok);
    }
    return result;
}

Variant encode_value(const Variant &value, int depth, bool &ok)
{
    if (value.get_type() == Variant::OBJECT)
    {
        const Ref<Resource> resource = value;
        if (resource.is_null() && Object::cast_to<Object>(value) != nullptr)
            ok = false;
        return encode_resource(resource, depth, ok);
    }
    if (value.get_type() == Variant::ARRAY)
    {
        const Array source = value;
        Array result;
        for (int64_t i = 0; i < source.size(); ++i)
            result.push_back(encode_value(source[i], depth + 1, ok));
        return result;
    }
    return value;
}

Variant decode_value(const Variant &data, int depth, bool &ok);

Ref<Resource> decode_resource(const Dictionary &data, int depth, bool &ok)
{
    if (depth >= MaxDepth)
    {
        ok = false;
        return Ref<Resource>();
    }
    if (data.has("path"))
    {
        const String path = data["path"];
        Ref<Resource> resource;
        if (is_file(path) && ResourceLoader::get_singleton()->exists(path))
            resource = ResourceLoader::get_singleton()->load(path);
        ok = ok && resource.is_valid();
        return resource;
    }
    const String name = data.get("class", String());
    if (!may_be_made(name))
    {
        ok = false;
        return Ref<Resource>();
    }
    Ref<Resource> resource = ClassDBSingleton::get_singleton()->instantiate(name);
    if (resource.is_null())
    {
        ok = false;
        return resource;
    }
    const Array names = data.keys();
    for (int64_t i = 0; i < names.size() && ok; ++i)
    {
        const String property = names[i];
        if (property != "class" && property != "script" && !property.begins_with("resource_"))
            resource->set(property, decode_value(data[names[i]], depth + 1, ok));
    }
    return resource;
}

Variant decode_value(const Variant &data, int depth, bool &ok)
{
    if (data.get_type() == Variant::DICTIONARY)
        return decode_resource(data, depth, ok);
    if (data.get_type() == Variant::ARRAY)
    {
        const Array source = data;
        Array result;
        for (int64_t i = 0; i < source.size() && ok; ++i)
            result.push_back(decode_value(source[i], depth + 1, ok));
        return result;
    }
    if (data.get_type() == Variant::OBJECT)
        ok = false; // only ever plain data
    return data;
}
} // namespace

Variant EditCommand::encode_sdf(const Ref<JarSignedDistanceField> &sdf)
{
    bool ok = true;
    const Variant result = encode_resource(sdf, 0, ok);
    return ok ? result : Variant();
}

Ref<JarSignedDistanceField> EditCommand::decode_sdf(const Variant &data)
{
    if (data.get_type() != Variant::DICTIONARY)
        return Ref<JarSignedDistanceField>();
    bool ok = true;
    const Ref<JarSignedDistanceField> sdf = decode_resource(data, 0, ok);
    return ok ? sdf : Ref<JarSignedDistanceField>();
}
