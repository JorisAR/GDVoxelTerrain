#ifndef OPERATION_SDF_H
#define OPERATION_SDF_H

#include "signed_distance_field.h"

// Combines two SDFs with a boolean operation (union / subtraction / intersection,
// plus smooth variants). Lets you carve caves (subtract a sphere), fuse shapes
// (union), or blend them (smooth ops) purely as data — no code.
//   result = op(sdf_a(p), sdf_b(p))
// Implements contributing-list item: "Combine multiple existing SDFs based on a
// boolean operation (OperationSDF)."
class JarOperationSdf : public JarSignedDistanceField
{
    GDCLASS(JarOperationSdf, JarSignedDistanceField);

  private:
    Ref<JarSignedDistanceField> _sdf_a;
    Ref<JarSignedDistanceField> _sdf_b;
    int _operation = SDF::SDF_OPERATION_UNION;
    float _smoothness = 1.0f; // blend radius k, only used by the smooth_* operations

  public:
    void set_sdf_a(const Ref<JarSignedDistanceField> &sdf) { _sdf_a = sdf; emit_changed(); }
    Ref<JarSignedDistanceField> get_sdf_a() const { return _sdf_a; }

    void set_sdf_b(const Ref<JarSignedDistanceField> &sdf) { _sdf_b = sdf; emit_changed(); }
    Ref<JarSignedDistanceField> get_sdf_b() const { return _sdf_b; }

    void set_operation(int op) { _operation = op; emit_changed(); }
    int get_operation() const { return _operation; }

    void set_smoothness(float k) { _smoothness = k; emit_changed(); }
    float get_smoothness() const { return _smoothness; }

    virtual float distance(const glm::vec3 &pos) const override
    {
        const bool ha = _sdf_a.is_valid();
        const bool hb = _sdf_b.is_valid();
        // Missing operands: fall back gracefully instead of dereferencing null.
        if (!ha && !hb)
            return 1e9f; // empty space
        if (!ha)
            return _sdf_b->distance(pos);
        if (!hb)
            return _sdf_a->distance(pos);

        const float a = _sdf_a->distance(pos);
        const float b = _sdf_b->distance(pos);
        return SDF::apply_operation(static_cast<SDF::Operation>(_operation), a, b,
                                    _smoothness > 0.0f ? _smoothness : 0.0001f);
    }

  protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("set_sdf_a", "sdf"), &JarOperationSdf::set_sdf_a);
        ClassDB::bind_method(D_METHOD("get_sdf_a"), &JarOperationSdf::get_sdf_a);
        ClassDB::bind_method(D_METHOD("set_sdf_b", "sdf"), &JarOperationSdf::set_sdf_b);
        ClassDB::bind_method(D_METHOD("get_sdf_b"), &JarOperationSdf::get_sdf_b);
        ClassDB::bind_method(D_METHOD("set_operation", "operation"), &JarOperationSdf::set_operation);
        ClassDB::bind_method(D_METHOD("get_operation"), &JarOperationSdf::get_operation);
        ClassDB::bind_method(D_METHOD("set_smoothness", "k"), &JarOperationSdf::set_smoothness);
        ClassDB::bind_method(D_METHOD("get_smoothness"), &JarOperationSdf::get_smoothness);

        ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sdf_a", PROPERTY_HINT_RESOURCE_TYPE, "JarSignedDistanceField"),
                     "set_sdf_a", "get_sdf_a");
        ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "sdf_b", PROPERTY_HINT_RESOURCE_TYPE, "JarSignedDistanceField"),
                     "set_sdf_b", "get_sdf_b");
        ADD_PROPERTY(PropertyInfo(Variant::INT, "operation", PROPERTY_HINT_ENUM,
                                  "Union,Subtraction,Intersection,Smooth Union,Smooth Subtraction,Smooth Intersection"),
                     "set_operation", "get_operation");
        ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smoothness", PROPERTY_HINT_RANGE, "0.0,64.0,0.01,or_greater"),
                     "set_smoothness", "get_smoothness");
    }
};

#endif // OPERATION_SDF_H
