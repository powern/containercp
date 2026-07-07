#ifndef CONTAINERCP_JOBS_JOB_MANAGER_H
#define CONTAINERCP_JOBS_JOB_MANAGER_H

#include "jobs/Job.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::jobs {

class JobManager {
public:
    uint64_t create(const std::string& type, const std::vector<std::string>& steps);
    void update(uint64_t id, const std::string& status, int progress, const std::string& message = "");
    Job* find(uint64_t id);
    const std::vector<Job>& list() const;

private:
    std::vector<Job> jobs_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::jobs

#endif // CONTAINERCP_JOBS_JOB_MANAGER_H
