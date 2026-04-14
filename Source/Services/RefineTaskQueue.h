#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace OpenTune {

class RefineTaskQueue {
public:
    using Task = std::function<void()>;

    RefineTaskQueue();
    ~RefineTaskQueue();

    void submit(uint64_t clipId, Task task);
    void cancel(uint64_t clipId);
    /** Drop all queued tasks (project load / new empty project). In-flight task may still run once. */
    void cancelAll();
    bool isPending(uint64_t clipId) const;

private:
    void workerLoop();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::map<uint64_t, Task> pending_;
    bool running_{true};
    std::thread worker_;
};

} // namespace OpenTune
