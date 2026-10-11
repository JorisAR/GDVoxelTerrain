#include "job_system.h"
#include <algorithm>
#include <memory>
#include <mutex>

namespace
{
std::unique_ptr<ThreadPool> s_pool;
std::mutex s_poolMutex;

size_t default_thread_count()
{
    // Leave room for Godot's main and render threads.
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 3 ? hw - 2 : 1;
}
} // namespace

ThreadPool &JobSystem::get_pool()
{
    std::lock_guard<std::mutex> lock(s_poolMutex);
    if (!s_pool)
        s_pool = std::make_unique<ThreadPool>(default_thread_count());
    return *s_pool;
}

void JobSystem::shutdown()
{
    std::lock_guard<std::mutex> lock(s_poolMutex);
    s_pool.reset(); // joins the workers
}
