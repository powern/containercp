#include "wordpress/WordPressCliJobService.h"

#include "wordpress/WordPressCliAudit.h"

#include <utility>
#include <vector>

namespace containercp::wordpress {
namespace {

std::vector<std::string> mutation_steps() {
    return {"Validating typed mutation", "Resolving managed runtime", "Verifying WP-CLI artifact",
            "Executing isolated mutation", "Cleaning runner", "Completed", "Mutation failed"};
}

WordPressCliMutationJobResult result(bool accepted,
                                     std::string code,
                                     std::string message,
                                     uint64_t job_id,
                                     uint64_t site_id,
                                     std::string operation,
                                     std::string package_id) {
    return {accepted, std::move(code), std::move(message), job_id, site_id,
            std::move(operation), std::move(package_id)};
}

} // namespace

WordPressCliJobService::WordPressCliJobService(jobs::JobManager& jobs,
                                               jobs::JobExecutor& executor,
                                               WordPressCliService& cli,
                                               logger::Logger& logger)
    : jobs_(jobs)
    , executor_(executor)
    , cli_(cli)
    , logger_(logger)
    , locks_(std::make_shared<QueueLockState>()) {
}

bool WordPressCliJobService::acquire(const std::shared_ptr<QueueLockState>& state, uint64_t site_id) {
    std::lock_guard<std::mutex> guard(state->mutex);
    return state->site_ids.insert(site_id).second;
}

void WordPressCliJobService::release(const std::shared_ptr<QueueLockState>& state, uint64_t site_id) {
    std::lock_guard<std::mutex> guard(state->mutex);
    state->site_ids.erase(site_id);
}

WordPressCliMutationJobResult WordPressCliJobService::enqueue(uint64_t site_id,
                                                              WordPressCliMutation mutation,
                                                              const std::string& package_id,
                                                              const std::string& admin_username,
                                                              const std::string& domain) {
    const std::string operation = wordPressCliMutationName(mutation);
    const auto locks = locks_;
    if (!acquire(locks, site_id)) {
        WordPressCliAuditLogger::log({0, site_id, domain, admin_username, operation, package_id,
                                      "rejected", "operation_already_running", false, false,
                                      WordPressCliAuditEvent::Level::Warning});
        return result(false, "operation_already_running", "A WordPress mutation is already queued for this site",
                      0, site_id, operation, package_id);
    }

    const uint64_t job_id = jobs_.create("wordpress-cli-" + operation, mutation_steps());
    jobs_.update(job_id, "pending", 0, "WordPress mutation queued");
    WordPressCliAuditLogger::log({job_id, site_id, domain, admin_username, operation, package_id,
                                  "queued", {}, false, false, WordPressCliAuditEvent::Level::Info});

    auto* cli = &cli_;
    auto* log = &logger_;
    const bool submitted = executor_.submit(job_id,
        [locks, cli, log, site_id, mutation, package_id, admin_username, domain](jobs::JobManager& jobs,
                                                                                  uint64_t queued_job_id) {
            struct Release {
                std::shared_ptr<QueueLockState> locks;
                uint64_t site_id;
                ~Release() { WordPressCliJobService::release(locks, site_id); }
            } release{locks, site_id};

            jobs.update(queued_job_id, "running", 10, "WordPress mutation running");
            const auto operation = wordPressCliMutationName(mutation);
            const auto cli_result = cli->run_mutation(site_id, mutation, package_id);
            jobs.update_cleanup(queued_job_id,
                                cli_result.cleanup_succeeded ? "succeeded" : "failed",
                                cli_result.cleanup_succeeded ? "Runner removed" : "Runner cleanup failed");

            jobs::JobFailureDiagnostics failure;
            failure.step = cli_result.cleanup_succeeded ? "Completed" : "Cleaning runner";
            failure.step_name = failure.step;
            failure.error_code = cli_result.failure_code;
            failure.reason = cli_result.failure_code.empty() ? "" : "Typed WordPress mutation failed";
            jobs.update_failure(queued_job_id, failure);
            jobs.update(queued_job_id, cli_result.success ? "completed" : "failed", 100,
                        cli_result.success ? "WordPress mutation completed" : "WordPress mutation failed");

            const auto level = cli_result.success ? WordPressCliAuditEvent::Level::Info
                                                  : WordPressCliAuditEvent::Level::Error;
            WordPressCliAuditLogger::log({queued_job_id, site_id, domain, admin_username, operation, package_id,
                                          cli_result.success ? "success" : "failure",
                                          cli_result.failure_code,
                                          cli_result.failure_code == "wordpress_cli_timeout",
                                          cli_result.cleanup_succeeded,
                                          level});
            if (!cli_result.success) {
                log->error("WORDPRESS", "Typed mutation failed: " + operation);
            }
        });
    if (!submitted) {
        release(locks, site_id);
        jobs_.update_cleanup(job_id, "not_started", "Runner was not started");
        jobs_.update(job_id, "failed", 0, "WordPress mutation queue unavailable");
        WordPressCliAuditLogger::log({job_id, site_id, domain, admin_username, operation, package_id,
                                      "failure", "queue_unavailable", false, false,
                                      WordPressCliAuditEvent::Level::Error});
        return result(false, "queue_unavailable", "WordPress mutation queue unavailable", job_id,
                      site_id, operation, package_id);
    }
    return result(true, "job_queued", "WordPress mutation job queued", job_id, site_id, operation, package_id);
}

} // namespace containercp::wordpress
