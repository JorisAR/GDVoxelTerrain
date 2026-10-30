#ifndef JAR_UNIVERSE_MATH_H
#define JAR_UNIVERSE_MATH_H

#include <cmath>
#include <glm/glm.hpp>
#include <godot_cpp/variant/transform3d.hpp>

using namespace godot;

// A rigid transform in double precision, for the universe's bookkeeping only
// (body placements over long times and large distances). The scene stays in
// floats: results are rounded to Transform3D at the very end.
struct DTransform
{
    glm::dmat3 basis{1.0};
    glm::dvec3 origin{0.0};

    DTransform() = default;
    DTransform(const glm::dmat3 &b, const glm::dvec3 &o) : basis(b), origin(o)
    {
    }
    static DTransform translation(const glm::dvec3 &o)
    {
        return DTransform(glm::dmat3(1.0), o);
    }
    // Rotation by `angle` radians about the unit `axis` (right-handed, as Godot's Basis).
    static glm::dmat3 rotation(const glm::dvec3 &axis, double angle)
    {
        const double c = std::cos(angle), s = std::sin(angle), t = 1.0 - c;
        const double x = axis.x, y = axis.y, z = axis.z;
        // glm is column-major: m[column][row].
        glm::dmat3 m;
        m[0] = glm::dvec3(t * x * x + c, t * x * y + s * z, t * x * z - s * y);
        m[1] = glm::dvec3(t * x * y - s * z, t * y * y + c, t * y * z + s * x);
        m[2] = glm::dvec3(t * x * z + s * y, t * y * z - s * x, t * z * z + c);
        return m;
    }

    static DTransform from(const Transform3D &t)
    {
        DTransform d;
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
                d.basis[c][r] = double(t.basis.rows[r][c]);
        d.origin = glm::dvec3(t.origin.x, t.origin.y, t.origin.z);
        return d;
    }
    Transform3D to() const
    {
        Transform3D t;
        for (int c = 0; c < 3; ++c)
            for (int r = 0; r < 3; ++r)
                t.basis.rows[r][c] = real_t(basis[c][r]);
        t.origin = Vector3(real_t(origin.x), real_t(origin.y), real_t(origin.z));
        return t;
    }
    static glm::dvec3 from(const Vector3 &v)
    {
        return glm::dvec3(v.x, v.y, v.z);
    }
    static Vector3 to(const glm::dvec3 &v)
    {
        return Vector3(real_t(v.x), real_t(v.y), real_t(v.z));
    }

    DTransform operator*(const DTransform &o) const
    {
        return DTransform(basis * o.basis, basis * o.origin + origin);
    }
    glm::dvec3 xform(const glm::dvec3 &p) const
    {
        return basis * p + origin;
    }
    // Rigid transforms (orthonormal bases, any scale is uniform): the inverse
    // through the general matrix inverse, so scaled nodes work too.
    DTransform inverse() const
    {
        const glm::dmat3 inv = glm::inverse(basis);
        return DTransform(inv, -(inv * origin));
    }
};

#endif // JAR_UNIVERSE_MATH_H
