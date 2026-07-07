#ifndef CONTAINERCP_JOBS_JOB_H
#define CONTAINERCP_JOBS_JOB_H

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::jobs {

struct Job {
    uint64_t id = 0;
    std::string type;
    std::string status = "pending";
    int progress = 0;
    std::vector<std::string> steps;
    int current_step = 0;
    std::string message;
    std::string created_at;
    std::string completed_at;
};

} // namespace containercp::jobs

#endif // CONTAINERCP_JOBS_JOB_H
