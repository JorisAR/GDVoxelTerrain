#ifndef UTILS_H
#define UTILS_H

#include "bounds.h"
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/string.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <vector>

namespace godot
{

class Utils : public Object
{
    GDCLASS(Utils, Object);

  public:
    Utils()
    {
    }
    ~Utils()
    {
    }

    static PackedFloat32Array vector_to_array_float(const std::vector<float> &vec)
    {
        PackedFloat32Array array;
        array.resize(vec.size());
        std::copy(vec.begin(), vec.end(), array.ptrw());
        return array;
    }

    static std::vector<float> array_to_vector_float(const PackedFloat32Array &array)
    {
        std::vector<float> vec(array.size());
        std::copy(array.ptr(), array.ptr() + array.size(), vec.begin());
        return vec;
    }

    static PackedInt32Array vector_to_array_int(const std::vector<int> &vec)
    {
        PackedInt32Array array;
        array.resize(vec.size());
        std::copy(vec.begin(), vec.end(), array.ptrw());
        return array;
    }

    static std::vector<int> array_to_vector_int(const PackedInt32Array &array)
    {
        std::vector<int> vec(array.size());
        std::copy(array.ptr(), array.ptr() + array.size(), vec.begin());
        return vec;
    }

    static godot::String vector_to_string_float(const std::vector<float> &vec)
    {
        godot::String string = "[";
        for (const float &f : vec)
        {
            string += String::num(f, 2) + ", ";
        }
        return string + "]";
    }

    static godot::String to_string(const Bounds &bounds)
    {
        return godot::String(("{" + glm::to_string(bounds.min_corner) + ", " + glm::to_string(bounds.max_corner) + "}").c_str());
    }

    template<class matType>
    static godot::String to_string(const matType &v)
    {
        return godot::String(("{" + glm::to_string(v) + "}").c_str());
    }

    static glm::vec3 to_vec3(const godot::Vector3 &v)
    {
        return glm::vec3(v.x, v.y, v.z);
    }

    static godot::Vector3 to_vector3(const glm::vec3 &v)
    {
        return godot::Vector3(v.x, v.y, v.z);
    }
};

} // namespace godot

#endif
