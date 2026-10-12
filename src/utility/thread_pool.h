#include <iostream>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

class ThreadPool {
public:
    ThreadPool(size_t numThreads);
    ~ThreadPool();
    void enqueue(std::function<void()> task);
    // Low priority work (e.g. population): runs only when no regular task is
    // waiting, and on at most half the workers, so terrain builds and meshing
    // started meanwhile are not stuck behind it.
    void enqueue_background(std::function<void()> task);
    // Runs fn(0..count-1) on the pool and blocks until all calls returned.
    // Indices are handed out dynamically, so uneven work balances itself.
    // Must not be called from a pool thread (it would wait on itself).
    void parallel_for(size_t count, const std::function<void(size_t)> &fn);
    
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::queue<std::function<void()>> backgroundTasks;
    size_t runningBackground = 0;
    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop;
    
    void workerThread();
};
