#ifndef TRANSFORM_SDF_H
#define TRANSFORM_SDF_H

#include "signed_distance_field.h"
#include <godot_cpp/variant/transform3d.hpp>

// Positions / rotates / scales any child SDF. Combined with JarOperationSdf this
// makes a full CSG toolkit: e.g. carve a *tilted* tunnel, place a rotated box.
// distance() maps the query point into the child's local space, evaluates the
// child there, and rescales the result by the transform's (uniform) scale.
// (Non-uniform scale is approximated with the smallest axis, which keeps the
// distance a conservative bound.)
class JarTransformSdf : public JarSignedDistanceField
{
    GDCLASS(JarTransformSdf, JarSignedDistanceField);

  private:
    Ref<JarSignedDistanceField> _sdf;
    Transform3D _transform;
    Transform3D _inv;    // cached affine inverse
    float _scale = 1.0f; // cached scale factor

    void recompute()
    {
        _inv = _transform.affine_inverse();
        const Vector3 s = _transform.basis.get_scale().abs();
        _scale = std::max(float(std::min(s.x, std::min(s.y, s.z))), 0.0001f);
    }

  public:
    JarTransformSdf() { recompute(); }

    void set_sdf(const Ref<JarSignedDistanceField> &s) { set_and_notify(_sdf, s); }
    Ref<JarSignedDistanceField> get_sdf() const { return _sdf; }
    void set_transform(const Transform3D &t)
    {
        _transform = t;
        recompute();
        emit_changed();
    }
    Transform3D get_transform() const { return _transform; }

    float distance(const glm::vec3 &pos) const override
    {
        if (_sdf.is_null())
            return 1e9f;
        const Vector3 local = _inv.xform(Vector3(pos.x, pos.y, pos.z));
        return _sdf->distance(glm::vec3(local.x, local.y, local.z)) * _scale;
    }

    Bounds bounds() const override
    {
        if (_sdf.is_null())
            return Bounds();
        const Basis &b = _transform.basis;
        const Vector3 &o = _transform.origin;
        const glm::mat4 m(glm::vec4(b.rows[0].x, b.rows[1].x, b.rows[2].x, 0.0f),
                          glm::vec4(b.rows[0].y, b.rows[1].y, b.rows[2].y, 0.0f),
                          glm::vec4(b.rows[0].z, b.rows[1].z, b.rows[2].z, 0.0f), glm::vec4(o.x, o.y, o.z, 1.0f));
        return _sdf->bounds().transformed(m);
    }

    void prepare() override
    {
        if (_sdf.is_valid())
            _sdf->prepare();
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_sdf", "sdf"), &JarTransformSdf::set_sdf);
        ClassDB::bind_method(D_METHOD("get_sdf"), &JarTransformSdf::get_sdf);
        ClassDB::bind_method(D_METHOD("set_transform", "transform"), &JarTransformSdf::set_transform);
        ClassDB::bind_method(D_METHOD("get_transform"), &JarTransformSdf::get_transform);
        ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sdf", PROPERTY_HINT_RESOURCE_TYPE, "JarSignedDistanceField"),
                     "set_sdf", "get_sdf");
        ADD_PROPERTY(PropertyInfo(Variant::TRANSFORM3D, "transform"), "set_transform", "get_transform");
    }
};

#endif // TRANSFORM_SDF_H
