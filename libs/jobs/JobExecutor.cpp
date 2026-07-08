#include "JobExecutor.h"

namespace containercp::jobs {

JobExecutor::JobExecutor(JobManager& jobs, size_t worker_count, size_t max_queue)
    : jobs_(jobs)
    , worker_count_(worker_count)
    , max_queue_(max_queue)
{
}

JobExecutor::~JobExecutor() {
    shutdown();
}

void JobExecutor::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    running_ = true;
    for (size_t i = 0; i < worker_count_; ++i) {
        workers_.emplace_back(&JobExecutor::worker_loop, this);
    }
}

void JobExecutor::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        running_ = false;
    }
    cv_.notify_all();

    for (auto& w : workers_) {
        if (w.joinable()) {
            w.join();
        }
    }
    workers_.clear();

    // Cancel any remaining pending tasks
    while (!queue_.empty()) {
        auto [job_id, _] = queue_.front();
        queue_.pop();
        jobs_.update(job_id, "cancelled", 0, "Daemon shutting down");
    }
}

bool JobExecutor::submit(uint64_t job_id, Task task) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) return false;
    if (queue_.size() >= max_queue_) {
        jobs_.update(job_id, "failed", 0, "Task queue full");
        return false;
    }
    queue_.push({job_id, std::move(task)});
    cv_.notify_one();
    return true;
}

void JobExecutor::worker_loop() {
    while (true) {
        std::pair<uint64_t, Task> item;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return !running_ || !queue_.empty();
            });
            if (!running_ && queue_.empty()) {
                return;
            }
            item = std::move(queue_.front());
            queue_.pop();
        }
        try {
            item.second(jobs_, item.first);
        } catch (const std::exception& e) {
            jobs_.update(item.first, "failed", 0, std::string("Exception: ") + e.what());
        } catch (...) {
            jobs_.update(item.first, "failed", 0, "Unknown exception");
        }
    }
}

} // namespace containercp::jobs
