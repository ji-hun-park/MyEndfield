#include "ThreadPool.h"

namespace Endfield {

ThreadPool::ThreadPool(size_t threads)
    : stop(false), active_tasks(0)
{
    for(size_t i = 0; i < threads; ++i) {
        workers.emplace_back(
            [this] {
                for(;;) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock,
                            [this]{ return this->stop || !this->tasks.empty(); });
                        if(this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    task();
                    
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        --active_tasks;
                        if (active_tasks == 0 && this->tasks.empty()) {
                            wait_condition.notify_all();
                        }
                    }
                }
            }
        );
    }
}

void ThreadPool::Enqueue(std::function<void()> task)
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        tasks.emplace(std::move(task));
        ++active_tasks;
    }
    condition.notify_one();
}

void ThreadPool::WaitAll()
{
    std::unique_lock<std::mutex> lock(queue_mutex);
    wait_condition.wait(lock, [this]{ return this->tasks.empty() && active_tasks == 0; });
}

ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for(std::thread &worker: workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

}

