#ifndef JAR_EDIT_STREAM_H
#define JAR_EDIT_STREAM_H

#include <cstdint>
#include <cstring>
#include <vector>

// The bytes of a save (JarVoxelVolume::save_edits): plain values, in order.
// Reading past the end sets `ok` to false and gives zeros, so damaged data is
// found out without ever reading outside it.
class EditWriter
{
  public:
    std::vector<uint8_t> bytes;
    template <typename T> void put(const T &value)
    {
        const size_t at = bytes.size();
        bytes.resize(at + sizeof(T));
        std::memcpy(bytes.data() + at, &value, sizeof(T));
    }
};

class EditReader
{
  public:
    const uint8_t *data;
    size_t size;
    size_t at = 0;
    bool ok = true;
    EditReader(const uint8_t *p_data, size_t p_size) : data(p_data), size(p_size) {}
    template <typename T> T get()
    {
        T value{};
        if (at + sizeof(T) > size)
        {
            ok = false;
            return value;
        }
        std::memcpy(&value, data + at, sizeof(T));
        at += sizeof(T);
        return value;
    }
};

#endif // JAR_EDIT_STREAM_H
