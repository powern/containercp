#ifndef CONTAINERCP_JOBS_JOB_EXECUTOR_H
#define CONTAINERCP_JOBS_JOB_EXECUTOR_H

#include "jobs/JobManager.h"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace containercp::jobs {

class JobExecutor {
public:
    using Task = std::function<void(JobManager&, uint64_t)>;

    JobExecutor(JobManager& jobs, size_t worker_count = 2, size_t max_queue = 64);
    ~JobExecutor();

    void start();
    void shutdown();

    bool submit(uint64_t job_id, Task task);

private:
    void worker_loop();

    JobManager& jobs_;
    std::vector<std::thread> workers_;
    std::queue<std::pair<uint64_t, Task>> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    size_t worker_count_;
    size_t max_queue_;
    bool running_ = false;
};

} // namespace containercp::jobs

#endif // CONTAINERCP_JOBS_JOB_EXECUTOR_H
