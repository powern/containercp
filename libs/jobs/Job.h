#ifndef CONTAINERCP_JOBS_JOB_H
#define CONTAINERCP_JOBS_JOB_H

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::jobs {

struct JobStep {
    std::string id;
    std::string name;
    bool started = false;
    bool completed = false;
    bool failed = false;
    bool skipped = true;
    uint64_t duration_ms = 0;
    std::string result;
    std::string message;
    std::string error_code;
    std::string started_at;
    std::string completed_at;
};

struct JobFailureDiagnostics {
    std::string step;
    std::string step_name;
    std::string reason;
    std::string error_code;
    bool compensation_started = false;
    std::string compensation_result = "not_started";
    bool manual_recovery_required = false;
};

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
    std::vector<JobStep> step_details;
    JobFailureDiagnostics failure;
};

} // namespace containercp::jobs

#endif // CONTAINERCP_JOBS_JOB_H
