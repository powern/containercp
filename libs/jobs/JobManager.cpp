#include "JobManager.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace containercp::jobs {

uint64_t JobManager::create(const std::string& type, const std::vector<std::string>& steps) {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ts;
    ts << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");

    Job j;
    j.id = next_id_++;
    j.type = type;
    j.status = "pending";
    j.progress = 0;
    j.steps = steps;
    for (const auto& step_name : steps) {
        JobStep step;
        step.id = step_name;
        step.name = step_name;
        j.step_details.push_back(std::move(step));
    }
    j.current_step = 0;
    j.created_at = ts.str();
    jobs_.push_back(std::move(j));
    return j.id;
}

void JobManager::update(uint64_t id, const std::string& status, int progress, const std::string& message) {
    for (auto& j : jobs_) {
        if (j.id == id) {
            j.status = status;
            j.progress = progress;
            if (!message.empty()) j.message = message;
            if (status == "running") {
                j.current_step = progress / (100 / std::max((int)j.steps.size(), 1));
            }
            if (status == "completed" || status == "failed") {
                auto now = std::chrono::system_clock::now();
                auto tt = std::chrono::system_clock::to_time_t(now);
                std::ostringstream ts;
                ts << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
                j.completed_at = ts.str();
            }
            return;
        }
    }
}

void JobManager::update_step_details(uint64_t id, const std::vector<JobStep>& steps) {
    for (auto& j : jobs_) {
        if (j.id == id) {
            j.step_details = steps;
            return;
        }
    }
}

void JobManager::update_failure(uint64_t id, const JobFailureDiagnostics& failure) {
    for (auto& j : jobs_) {
        if (j.id == id) {
            j.failure = failure;
            return;
        }
    }
}

Job* JobManager::find(uint64_t id) {
    for (auto& j : jobs_) {
        if (j.id == id) return &j;
    }
    return nullptr;
}

const std::vector<Job>& JobManager::list() const {
    return jobs_;
}

} // namespace containercp::jobs
