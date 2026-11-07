#include "voxel_viewer.h"
#include <algorithm>

std::vector<JarVoxelViewer *> JarVoxelViewer::s_viewers;

void JarVoxelViewer::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("is_enabled"), &JarVoxelViewer::is_enabled);
    ClassDB::bind_method(D_METHOD("set_enabled", "value"), &JarVoxelViewer::set_enabled);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
    ClassDB::bind_method(D_METHOD("get_predict_motion"), &JarVoxelViewer::get_predict_motion);
    ClassDB::bind_method(D_METHOD("set_predict_motion", "value"), &JarVoxelViewer::set_predict_motion);
    ClassDB::bind_method(D_METHOD("get_predict_seconds"), &JarVoxelViewer::get_predict_seconds);
    ClassDB::bind_method(D_METHOD("set_predict_seconds", "value"), &JarVoxelViewer::set_predict_seconds);
    ClassDB::bind_method(D_METHOD("get_predict_max_distance"), &JarVoxelViewer::get_predict_max_distance);
    ClassDB::bind_method(D_METHOD("set_predict_max_distance", "value"), &JarVoxelViewer::set_predict_max_distance);
    ADD_GROUP("Prediction", "predict_");
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "predict_motion"), "set_predict_motion", "get_predict_motion");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "predict_seconds", PROPERTY_HINT_RANGE, "0,5,0.05,or_greater,suffix:s"),
                 "set_predict_seconds", "get_predict_seconds");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "predict_max_distance", PROPERTY_HINT_RANGE, "0,20000,1,or_greater,suffix:m"),
                 "set_predict_max_distance", "get_predict_max_distance");
}

void JarVoxelViewer::_notification(int what)
{
    switch (what)
    {
    case NOTIFICATION_ENTER_TREE:
        s_viewers.push_back(this);
        break;
    case NOTIFICATION_EXIT_TREE:
        s_viewers.erase(std::remove(s_viewers.begin(), s_viewers.end(), this), s_viewers.end());
        break;
    }
}
