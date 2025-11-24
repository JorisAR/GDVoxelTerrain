#ifndef FEATURE_NODE_H
#define FEATURE_NODE_H

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

class FeatureNode3D : public Node3D {
    GDCLASS(FeatureNode3D, Node3D);

protected:
    static void _bind_methods() {
        // Signals
        ADD_SIGNAL(MethodInfo("entered_range"));
        ADD_SIGNAL(MethodInfo("exited_range"));

        // Properties
        ClassDB::bind_method(D_METHOD("set_toggle_visibility", "enable"), &FeatureNode3D::set_toggle_visibility);
        ClassDB::bind_method(D_METHOD("get_toggle_visibility"), &FeatureNode3D::get_toggle_visibility);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "toggle_visibility"), "set_toggle_visibility", "get_toggle_visibility");

        ClassDB::bind_method(D_METHOD("set_toggle_process", "enable"), &FeatureNode3D::set_toggle_process);
        ClassDB::bind_method(D_METHOD("get_toggle_process"), &FeatureNode3D::get_toggle_process);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "toggle_process"), "set_toggle_process", "get_toggle_process");

        ClassDB::bind_method(D_METHOD("set_toggle_physics", "enable"), &FeatureNode3D::set_toggle_physics);
        ClassDB::bind_method(D_METHOD("get_toggle_physics"), &FeatureNode3D::get_toggle_physics);
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "toggle_physics"), "set_toggle_physics", "get_toggle_physics");
    }

public:
    FeatureNode3D() = default;
    ~FeatureNode3D() = default;

    void toggle(bool enable) {
        if (enable == _active) return;
        _active = enable;
        apply_toggles(enable);
        if( enable ) {
            emit_signal("entered_range");
        } else {
            emit_signal("exited_range");
        }
    }

    bool is_active() const { return _active; }

    // --- Property setters/getters ---
    void set_toggle_visibility(bool enable) { _toggle_visibility = enable; }
    bool get_toggle_visibility() const { return _toggle_visibility; }

    void set_toggle_process(bool enable) { _toggle_process = enable; }
    bool get_toggle_process() const { return _toggle_process; }

    void set_toggle_physics(bool enable) { _toggle_physics = enable; }
    bool get_toggle_physics() const { return _toggle_physics; }

private:
    bool _active = false;
    bool _toggle_visibility = true;
    bool _toggle_process = true;
    bool _toggle_physics = true;

    void apply_toggles(bool enable) {
        if (_toggle_visibility) {
            set_visible(enable);
        }
        if (_toggle_process) {
            set_process(enable);
        }
        if (_toggle_physics) {
            set_physics_process(enable);
        }
    }
};

#endif // FEATURE_NODE_H
