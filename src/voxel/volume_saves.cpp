// Saving and loading what edits changed (see JarVoxelVolume::save_edits).
#include "edit_stream.h"
#include "voxel_volume.h"
#include <chrono>
#include <godot_cpp/classes/file_access.hpp>
#include <thread>

namespace
{
constexpr uint32_t Magic = 0x3145564A; // "JVE1"
constexpr uint32_t Version = 1;
constexpr size_t OuterHeader = sizeof(uint32_t) + sizeof(uint64_t); // magic, unpacked size
constexpr uint64_t MaxUnpacked = uint64_t(1) << 34;
} // namespace

// Main thread: queued edits applied, the octree at rest (mesh tasks only read it).
void JarVoxelVolume::settle_edits()
{
    for (;;)
    {
        {
            std::unique_lock<std::mutex> lock(_buildMutex);
            _buildDoneCv.wait(lock, [this] { return !_isBuilding; });
        }
        if (_modifySettingsQueue.empty())
            return;
        while (_meshComputeScheduler->is_running() || _extensionTasks > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        process_modify_queue();
    }
}

PackedByteArray JarVoxelVolume::save_edits()
{
    PackedByteArray result;
    if (!_pendingRestore.is_empty())
        return _pendingRestore; // loaded, not built yet: still what it was
    EditWriter writer;
    writer.put(Version);
    writer.put(_voxelSize);
    writer.put(_extent);
    if (_voxelRoot)
    {
        settle_edits();
        const bool edited = _voxelRoot->has_edits();
        writer.put(uint8_t(edited ? 1 : 0));
        if (edited)
            _voxelRoot->save_edits(writer);
        std::vector<uint8_t> state;
        {
            std::shared_lock<std::shared_mutex> lock(_extensionsMutex);
            for (VolumeExtension *extension : _extensions)
                extension->save_state(state);
        }
        writer.put(uint64_t(state.size()));
        writer.bytes.insert(writer.bytes.end(), state.begin(), state.end());
    }
    else
    {
        writer.put(uint8_t(0));
        writer.put(uint64_t(0));
    }
    PackedByteArray raw;
    raw.resize(int64_t(writer.bytes.size()));
    std::memcpy(raw.ptrw(), writer.bytes.data(), writer.bytes.size());
    const PackedByteArray packed = raw.compress(FileAccess::COMPRESSION_ZSTD);
    result.resize(int64_t(OuterHeader) + packed.size());
    EditWriter header;
    header.put(Magic);
    header.put(uint64_t(writer.bytes.size()));
    std::memcpy(result.ptrw(), header.bytes.data(), OuterHeader);
    std::memcpy(result.ptrw() + OuterHeader, packed.ptr(), size_t(packed.size()));
    return result;
}

PackedByteArray JarVoxelVolume::unpack_edits(const PackedByteArray &data) const
{
    PackedByteArray raw;
    if (size_t(data.size()) <= OuterHeader)
        return raw;
    EditReader header(data.ptr(), size_t(data.size()));
    const uint32_t magic = header.get<uint32_t>();
    const uint64_t size = header.get<uint64_t>();
    if (magic != Magic || size == 0 || size > MaxUnpacked)
        return raw;
    return data.slice(OuterHeader).decompress(int64_t(size), FileAccess::COMPRESSION_ZSTD);
}

Error JarVoxelVolume::load_edits(const PackedByteArray &data)
{
    const PackedByteArray raw = unpack_edits(data);
    ERR_FAIL_COND_V_MSG(raw.is_empty(), ERR_FILE_CORRUPT, "These bytes are not saved edits.");
    EditReader reader(raw.ptr(), size_t(raw.size()));
    const uint32_t version = reader.get<uint32_t>();
    const float voxelSize = reader.get<float>();
    const float extent = reader.get<float>();
    ERR_FAIL_COND_V_MSG(version != Version, ERR_FILE_UNRECOGNIZED, "Saved edits of another version.");
    ERR_FAIL_COND_V_MSG(voxelSize != _voxelSize || extent != _extent, ERR_INVALID_DATA,
                        "Saved edits of a volume with another voxel_size or extent.");
    const int depth = std::max(ChunkSizeLog2 + 1, int(std::ceil(std::log2(_extent / _voxelSize))));
    if (reader.get<uint8_t>() != 0)
        VoxelOctreeNode::check_edits(reader, depth);
    const uint64_t stateSize = reader.get<uint64_t>();
    ERR_FAIL_COND_V_MSG(!reader.ok || reader.at + stateSize != reader.size, ERR_FILE_CORRUPT, "Saved edits are damaged.");

    // The volume starts over from these edits. Anything edited so far is gone.
    teardown();
    while (!_modifySettingsQueue.empty())
        _modifySettingsQueue.pop();
    _queuedLargeEdit = false;
    _pendingRestore = raw;
    _everEdited = true;
    _restoredEdits = true;
    _pendingState.assign(raw.ptr() + reader.at, raw.ptr() + reader.at + stateSize);
    if (is_inside_tree() && is_active())
        initialize();
    return OK;
}

// Main thread, before the first build: extensions (children) are attached by
// then, also when the load came before the volume entered the tree.
void JarVoxelVolume::deliver_saved_state()
{
    if (_pendingState.empty())
        return;
    {
        std::shared_lock<std::shared_mutex> lock(_extensionsMutex);
        for (VolumeExtension *extension : _extensions)
            extension->load_state(_pendingState.data(), _pendingState.size());
    }
    _pendingState.clear();
}

// Build worker, before the first build.
void JarVoxelVolume::restore_edits(const PackedByteArray &raw)
{
    EditReader reader(raw.ptr(), size_t(raw.size()));
    reader.get<uint32_t>();
    reader.get<float>();
    reader.get<float>();
    if (reader.get<uint8_t>() != 0)
        _voxelRoot->load_edits(reader, *this);
}

bool JarVoxelVolume::ground_changed_at(const glm::vec3 &point)
{
    if (_sdf.is_null())
        return false;
    return std::abs(field_at(point) - _sdf->distance(point)) > 0.5f * _voxelSize;
}
