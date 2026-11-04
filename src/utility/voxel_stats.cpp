#include "voxel_stats.h"
#include <godot_cpp/classes/performance.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <algorithm>
#include <mutex>

namespace VoxelStats
{
namespace
{
const char *const StageNames[StageCount] = {
    "volume",  "upload", "commit",  "colliders", "chunk create",    "edit wait", "details", "features",
    "extensions", "universe", "layers", "build", "edit",     "mesh",      "collider data", "extension", "details",
    "features"};
const char *const CountNames[CountCount] = {"chunks meshed", "chunks committed", "chunks created", "colliders built",
                                             "edit nodes"};
const char *const GaugeNames[GaugeCount] = {"mesh queue", "collider queue"};

bool is_main(int stage)
{
    return stage < WorkerBuild;
}

std::atomic<int64_t> s_time[StageCount];
std::atomic<int64_t> s_count[CountCount];
std::atomic<int64_t> s_gauge[GaugeCount];
thread_local Scope *t_current = nullptr;

// Main thread only (tick, the getters and the monitors run there).
std::mutex s_mutex;
uint64_t s_frame = 0;
double s_lastTime[StageCount] = {};
int64_t s_lastCount[CountCount] = {};
// The second being collected, and the last full one.
double s_sum[StageCount] = {}, s_max[StageCount] = {};
double s_countSum[CountCount] = {};
int s_frames = 0;
std::chrono::steady_clock::time_point s_secondStart = std::chrono::steady_clock::now();
double s_avg[StageCount] = {}, s_worst[StageCount] = {};
double s_countAvg[CountCount] = {};
bool s_registered = false;

enum MonitorKind
{
    StageAverage,
    StageWorst,
    CountAverage,
    GaugeValue
};

double monitor(int kind, int index)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    switch (kind)
    {
    case StageAverage:
        return s_avg[index];
    case StageWorst:
        return s_worst[index];
    case CountAverage:
        return s_countAvg[index];
    default:
        return double(s_gauge[index].load());
    }
}

String monitor_id(int kind, int index)
{
    switch (kind)
    {
    case StageAverage:
        return String(is_main(index) ? "JarVoxel main ms (avg)/" : "JarVoxel workers ms (avg)/") + StageNames[index];
    case StageWorst:
        return String(is_main(index) ? "JarVoxel main ms (worst)/" : "JarVoxel workers ms (worst)/") +
               StageNames[index];
    case CountAverage:
        return String("JarVoxel per frame (avg)/") + CountNames[index];
    default:
        return String("JarVoxel queues/") + GaugeNames[index];
    }
}

template <typename F> void for_each_monitor(F f)
{
    for (int i = 0; i < StageCount; ++i)
    {
        f(StageAverage, i);
        f(StageWorst, i);
    }
    for (int i = 0; i < CountCount; ++i)
        f(CountAverage, i);
    for (int i = 0; i < GaugeCount; ++i)
        f(GaugeValue, i);
}
} // namespace

void add_time(Stage stage, int64_t nanoseconds)
{
    s_time[stage].fetch_add(nanoseconds, std::memory_order_relaxed);
}

void add_count(Count count, int64_t amount)
{
    s_count[count].fetch_add(amount, std::memory_order_relaxed);
}

void set_gauge(Gauge gauge, int64_t value)
{
    s_gauge[gauge].store(value, std::memory_order_relaxed);
}

void tick(uint64_t frame)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    if (frame == s_frame)
        return;
    s_frame = frame;
    for (int i = 0; i < StageCount; ++i)
    {
        s_lastTime[i] = double(s_time[i].exchange(0, std::memory_order_relaxed)) * 1e-6;
        s_sum[i] += s_lastTime[i];
        s_max[i] = std::max(s_max[i], s_lastTime[i]);
    }
    for (int i = 0; i < CountCount; ++i)
    {
        s_lastCount[i] = s_count[i].exchange(0, std::memory_order_relaxed);
        s_countSum[i] += double(s_lastCount[i]);
    }
    s_frames++;
    const auto now = std::chrono::steady_clock::now();
    if (now - s_secondStart < std::chrono::seconds(1))
        return;
    for (int i = 0; i < StageCount; ++i)
    {
        s_avg[i] = s_sum[i] / s_frames;
        s_worst[i] = s_max[i];
        s_sum[i] = s_max[i] = 0.0;
    }
    for (int i = 0; i < CountCount; ++i)
    {
        s_countAvg[i] = s_countSum[i] / s_frames;
        s_countSum[i] = 0.0;
    }
    s_frames = 0;
    s_secondStart = now;
}

Scope::Scope(Stage stage) : _stage(stage), _start(std::chrono::steady_clock::now()), _parent(t_current)
{
    t_current = this;
}

Scope::~Scope()
{
    const int64_t total =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - _start).count();
    add_time(_stage, total - _nested);
    if (_parent != nullptr)
        _parent->_nested += total;
    t_current = _parent;
}

void register_monitors()
{
    Performance *performance = Performance::get_singleton();
    if (s_registered || performance == nullptr)
        return;
    s_registered = true;
    for_each_monitor([performance](int kind, int index) {
        const String id = monitor_id(kind, index);
        if (!performance->has_custom_monitor(id))
        {
            Array args;
            args.push_back(kind);
            args.push_back(index);
            performance->add_custom_monitor(id, callable_mp_static(&monitor), args);
        }
    });
}

void unregister_monitors()
{
    Performance *performance = Performance::get_singleton();
    if (!s_registered || performance == nullptr)
        return;
    s_registered = false;
    for_each_monitor([performance](int kind, int index) {
        const String id = monitor_id(kind, index);
        if (performance->has_custom_monitor(id))
            performance->remove_custom_monitor(id);
    });
}
} // namespace VoxelStats

void JarVoxelStats::_bind_methods()
{
    ClassDB::bind_static_method("JarVoxelStats", D_METHOD("get_frame"), &JarVoxelStats::get_frame);
    ClassDB::bind_static_method("JarVoxelStats", D_METHOD("get_second"), &JarVoxelStats::get_second);
}

Dictionary JarVoxelStats::get_frame()
{
    using namespace VoxelStats;
    std::lock_guard<std::mutex> lock(s_mutex);
    Dictionary main, workers, counts, gauges;
    for (int i = 0; i < StageCount; ++i)
        (is_main(i) ? main : workers)[StageNames[i]] = s_lastTime[i];
    for (int i = 0; i < CountCount; ++i)
        counts[CountNames[i]] = s_lastCount[i];
    for (int i = 0; i < GaugeCount; ++i)
        gauges[GaugeNames[i]] = s_gauge[i].load();
    Dictionary result;
    result["main"] = main;
    result["workers"] = workers;
    result["counts"] = counts;
    result["gauges"] = gauges;
    return result;
}

Dictionary JarVoxelStats::get_second()
{
    using namespace VoxelStats;
    std::lock_guard<std::mutex> lock(s_mutex);
    Dictionary main, workers;
    for (int i = 0; i < StageCount; ++i)
    {
        Array pair;
        pair.push_back(s_avg[i]);
        pair.push_back(s_worst[i]);
        (is_main(i) ? main : workers)[StageNames[i]] = pair;
    }
    Dictionary result;
    result["main"] = main;
    result["workers"] = workers;
    return result;
}
