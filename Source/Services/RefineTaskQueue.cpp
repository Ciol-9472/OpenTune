#include "RefineTaskQueue.h"

namespace OpenTune {

RefineTaskQueue::RefineTaskQueue()
{
    worker_ = std::thread([this]() { workerLoop(); });
}

RefineTaskQueue::~RefineTaskQueue()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void RefineTaskQueue::submit(uint64_t clipId, Task task)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_[clipId] = std::move(task);
    }
    cv_.notify_one();
}

void RefineTaskQueue::cancel(uint64_t clipId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.erase(clipId);
}

void RefineTaskQueue::cancelAll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
}

bool RefineTaskQueue::isPending(uint64_t clipId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.find(clipId) != pending_.end();
}

void RefineTaskQueue::workerLoop()
{
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return !running_ || !pending_.empty(); });
            if (!running_) {
                return;
            }
            auto it = pending_.begin();
            task = std::move(it->second);
            pending_.erase(it);
        }
        if (task) {
            task();
        }
    }
}

} // namespace OpenTune
