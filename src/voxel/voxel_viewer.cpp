#include "voxel_viewer.h"
#include <algorithm>

std::vector<JarVoxelViewer *> JarVoxelViewer::s_viewers;

void JarVoxelViewer::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("is_enabled"), &JarVoxelViewer::is_enabled);
    ClassDB::bind_method(D_METHOD("set_enabled", "value"), &JarVoxelViewer::set_enabled);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
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
