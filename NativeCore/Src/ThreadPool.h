#pragma once
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

namespace Endfield {

class ThreadPool {
public:
    ThreadPool(size_t threads);
    ~ThreadPool();
    
    void Enqueue(std::function<void()> task);
    void WaitAll();

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::condition_variable wait_condition;
    bool stop;
    std::atomic<int> active_tasks;
};

}

