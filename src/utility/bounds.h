#ifndef JAR_AABB_H
#define JAR_AABB_H

#include <glm/glm.hpp>
#include <limits>
#include <godot_cpp/variant/aabb.hpp>
#include <godot_cpp/variant/vector3.hpp>

struct Bounds
{
  public:
    glm::vec3 min;
    glm::vec3 max;

    Bounds() : min(std::numeric_limits<float>::max()), max(std::numeric_limits<float>::lowest())
    {
    }

    Bounds(const glm::vec3 &min, const glm::vec3 &max)
    {
        this->min = glm::min(min, max);
        this->max = glm::max(min, max);
    }

    Bounds(const godot::AABB &aabb)
    {
        godot::Vector3 godot_min = aabb.position;
        godot::Vector3 godot_max = aabb.position + aabb.size;
        min = {godot_min.x, godot_min.y, godot_min.z};
        max = {godot_max.x, godot_max.y, godot_max.z};
        // Ensure validity
        min = glm::min(min, max);
        max = glm::max(min, max);
    }

    // Effectively unbounded (large but finite, so arithmetic stays NaN-free).
    static Bounds infinite()
    {
        return Bounds(glm::vec3(-1e30f), glm::vec3(1e30f));
    }

    inline bool is_infinite() const
    {
        return glm::any(glm::lessThanEqual(min, glm::vec3(-1e29f))) ||
               glm::any(glm::greaterThanEqual(max, glm::vec3(1e29f)));
    }

    // Axis-aligned bounds of this box after an affine transform.
    inline Bounds transformed(const glm::mat4 &m) const
    {
        if (!is_valid() || is_infinite())
            return *this;
        Bounds result;
        for (int i = 0; i < 8; ++i)
        {
            const glm::vec3 corner((i & 1) ? max.x : min.x, (i & 2) ? max.y : min.y, (i & 4) ? max.z : min.z);
            const glm::vec3 p = glm::vec3(m * glm::vec4(corner, 1.0f));
            result.min = glm::min(result.min, p);
            result.max = glm::max(result.max, p);
        }
        return result;
    }

    inline bool is_valid() const
    {
        return (min.x <= max.x && min.y <= max.y && min.z <= max.z);
    }

    inline glm::vec3 get_center() const
    {
        return (min + max) * 0.5f;
    }

    inline glm::vec3 get_size() const
    {
        return max - min;
    }

    inline bool contains_point(const glm::vec3 &point) const
    {
        return (point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y && point.z >= min.z &&
                point.z <= max.z);
    }

    inline Bounds intersected(const Bounds &other) const
    {
        glm::vec3 new_min = glm::max(min, other.min);
        glm::vec3 new_max = glm::min(max, other.max);
        if (new_min.x > new_max.x || new_min.y > new_max.y || new_min.z > new_max.z)
        {
            return Bounds(); // Empty bounds
        }
        return Bounds(new_min, new_max);
    }

    inline Bounds joined(const Bounds &other) const
    {
        glm::vec3 new_min = glm::min(min, other.min);
        glm::vec3 new_max = glm::max(max, other.max);
        return Bounds(new_min, new_max);
    }

    // assumption: other or this do not enclose each other.
    inline Bounds subtracted(const Bounds &other) const
    {
        if (!intersects(other))
        {
            return *this; // No intersection, return the original bounds
        }

        glm::vec3 new_min = min;
        glm::vec3 new_max = max;

        // Remove the overlapping volume
        if (other.min.x <= max.x && other.max.x >= min.x)
        {
            if (other.min.x > min.x)
            {
                new_max.x = glm::min(max.x, other.min.x);
            }
            else if (other.max.x < max.x)
            {
                new_min.x = glm::max(min.x, other.max.x);
            }
        }

        if (other.min.y <= max.y && other.max.y >= min.y)
        {
            if (other.min.y > min.y)
            {
                new_max.y = glm::min(max.y, other.min.y);
            }
            else if (other.max.y < max.y)
            {
                new_min.y = glm::max(min.y, other.max.y);
            }
        }

        if (other.min.z <= max.z && other.max.z >= min.z)
        {
            if (other.min.z > min.z)
            {
                new_max.z = glm::min(max.z, other.min.z);
            }
            else if (other.max.z < max.z)
            {
                new_min.z = glm::max(min.z, other.max.z);
            }
        }

        return Bounds(new_min, new_max);
    }

    inline Bounds shaved_by_closest_plane(const Bounds &other) const {
        if (other.contains_point(this->get_center())) {
            return *this;
        }
    
        const glm::vec3 this_center = get_center();
        const glm::vec3 other_center = other.get_center();
        const glm::vec3 d = this_center - other_center;
        const glm::vec3 ad = glm::abs(d);
        const float min_ad = glm::min(glm::min(ad.x, ad.y), ad.z);
    
        // Mask for axes with smallest distance component
        const glm::vec3 mask = glm::step(ad, glm::vec3(min_ad)) * 
                              glm::step(glm::vec3(min_ad), ad);
        
        // Direction mask (1 for positive d, 0 for negative)
        const glm::vec3 use_max = glm::step(glm::vec3(0.0f), d);
    
        // Apply adjustment only to closest axis
        const glm::vec3 new_min = min * (1.0f - mask) + 
                                 (other.min * (1.0f - use_max) + min * use_max) * mask;
        const glm::vec3 new_max = max * (1.0f - mask) + 
                                 (other.max * use_max + max * (1.0f - use_max)) * mask;
    
        return Bounds(glm::min(new_min, new_max), glm::max(new_min, new_max));
    }

    inline Bounds expanded(const glm::vec3 &amount) const
    {
        return Bounds(min - amount, max + amount);
    }

    inline Bounds expanded(float amount) const
    {
        return expanded(glm::vec3(amount));
    }

    inline bool intersects(const Bounds &other) const
    {
        return (min.x <= other.max.x && max.x >= other.min.x && min.y <= other.max.y && max.y >= other.min.y &&
                min.z <= other.max.z && max.z >= other.min.z);
    }

    inline bool encloses(const Bounds &other) const
    {
        return (min.x <= other.min.x && max.x >= other.max.x && min.y <= other.min.y && max.y >= other.max.y &&
                min.z <= other.min.z && max.z >= other.max.z);
    }

    inline Bounds operator*(float scalar) const
    {
        return Bounds(min * scalar, max * scalar);
    }

    inline Bounds operator*(const glm::vec3 &vec) const
    {
        return Bounds(min * vec, max * vec);
    }

    inline Bounds operator+(const glm::vec3 &offset) const
    {
        return Bounds(min + offset, max + offset);
    }
};

#endif // JAR_AABB_H