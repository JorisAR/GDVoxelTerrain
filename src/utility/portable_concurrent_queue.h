#ifndef PORTABLE_CONCURRENT_QUEUE_H
#define PORTABLE_CONCURRENT_QUEUE_H

// Cross-platform stand-in for Microsoft's <concurrent_queue.h> /
// <concurrent_priority_queue.h> (the `concurrency::` types), which only exist
// on MSVC/Windows. This provides the small subset the plugin actually uses
// (push / try_pop / empty) with the same API, backed by a std container + mutex.
// Used on non-Windows so the plugin builds on Linux/macOS without touching any
// call sites; Windows keeps the native MS implementation.

#include <mutex>
#include <queue>
#include <vector>
#include <utility>

namespace concurrency {

template <typename T>
class concurrent_queue {
    std::queue<T> _q;
    mutable std::mutex _m;

  public:
    void push(const T &v) {
        std::lock_guard<std::mutex> lock(_m);
        _q.push(v);
    }

    bool try_pop(T &out) {
        std::lock_guard<std::mutex> lock(_m);
        if (_q.empty()) return false;
        out = std::move(_q.front());
        _q.pop();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(_m);
        return _q.empty();
    }
};

template <typename T, typename Compare>
class concurrent_priority_queue {
    std::priority_queue<T, std::vector<T>, Compare> _q;
    mutable std::mutex _m;

  public:
    void push(const T &v) {
        std::lock_guard<std::mutex> lock(_m);
        _q.push(v);
    }

    bool try_pop(T &out) {
        std::lock_guard<std::mutex> lock(_m);
        if (_q.empty()) return false;
        out = _q.top();
        _q.pop();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(_m);
        return _q.empty();
    }
};

} // namespace concurrency

#endif // PORTABLE_CONCURRENT_QUEUE_H
