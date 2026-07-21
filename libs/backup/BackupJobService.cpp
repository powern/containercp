#include "backup/BackupJobService.h"

#include <utility>
#include <vector>

namespace containercp::backup {
namespace {

BackupJobResult result(bool success,
                       std::string code,
                       std::string message,
                       std::string operation,
                       uint64_t job_id = 0,
                       uint64_t backup_id = 0,
                       uint64_t site_id = 0,
                       uint64_t recovery_backup_id = 0) {
    return {success, std::move(code), std::move(message), job_id, backup_id, site_id, recovery_backup_id, std::move(operation)};
}

std::vector<std::string> create_steps() {
    return {"Validating Site", "Resolving managed database", "Checking database runtime", "Preparing secure database credentials",
            "Exporting logical database dump", "Validating SQL dump", "Creating backup manifest", "Staging Site files",
            "Creating archive", "Validating archive", "Persisting backup record", "Cleaning staging", "Completed",
            "Backup failed, cleaning partial artifacts"};
}

std::vector<std::string> restore_steps() {
    return {"Validating backup", "Validating manifest and checksums", "Validating target Site", "Creating pre-restore recovery backup",
            "Preparing application consistency controls", "Restoring Site files", "Restoring managed database", "Verifying database access",
            "Verifying Site runtime", "Cleaning staging", "Completed", "Attempting automatic recovery", "Manual recovery required"};
}

} // namespace

BackupJobService::BackupJobService(jobs::JobManager& jobs,
                                   jobs::JobExecutor& executor,
                                   BackupService& backup_service,
                                   PersistCallback persist)
    : jobs_(jobs)
    , executor_(executor)
    , backup_service_(backup_service)
    , persist_(std::move(persist))
    , locks_(std::make_shared<QueueLockState>()) {
}

std::string BackupJobService::lock_key(uint64_t id, const std::string& operation) {
    return std::to_string(id) + ":" + operation;
}

bool BackupJobService::acquire(const std::shared_ptr<QueueLockState>& state, uint64_t id, const std::string& operation) {
    std::lock_guard<std::mutex> guard(state->mutex);
    return state->locks.insert(lock_key(id, operation)).second;
}

void BackupJobService::release(const std::shared_ptr<QueueLockState>& state, uint64_t id, const std::string& operation) {
    std::lock_guard<std::mutex> guard(state->mutex);
    state->locks.erase(lock_key(id, operation));
}

BackupJobResult BackupJobService::enqueue_create(uint64_t site_id) {
    const std::string operation = "create";
    auto locks = locks_;
    if (!acquire(locks, site_id, operation)) return result(false, "operation_already_running", "A backup create operation is already queued", operation, 0, 0, site_id);
    const uint64_t job_id = jobs_.create("backup-create", create_steps());
    jobs_.update(job_id, "pending", 0, "Backup create queued");
    auto* service = &backup_service_;
    auto persist = persist_;
    const bool submitted = executor_.submit(job_id, [locks, service, persist, site_id](jobs::JobManager& jobs, uint64_t queued_job_id) {
        struct Release {
            std::shared_ptr<QueueLockState> locks;
            uint64_t id;
            std::string operation;
            ~Release() { BackupJobService::release(locks, id, operation); }
        } release{locks, site_id, "create"};
        jobs.update(queued_job_id, "running", 10, "Backup create running");
        auto op_result = service->create_site_backup(site_id, queued_job_id);
        if (op_result.success && persist && !persist()) {
            op_result.success = false;
            op_result.code = "metadata_persist_failed";
            op_result.message = "Backup completed but metadata persistence failed";
            op_result.failure.step = "Persisting backup record";
            op_result.failure.step_name = "Persisting backup record";
            op_result.failure.reason = op_result.message;
            op_result.failure.error_code = op_result.code;
        }
        jobs.update_step_details(queued_job_id, op_result.steps);
        jobs.update_failure(queued_job_id, op_result.failure);
        jobs.update(queued_job_id, op_result.success ? "completed" : "failed", 100, op_result.message);
    });
    if (!submitted) {
        release(locks, site_id, operation);
        jobs_.update(job_id, "failed", 0, "Task queue unavailable");
        return result(false, "queue_unavailable", "Task queue unavailable", operation, job_id, 0, site_id);
    }
    return result(true, "job_queued", "Backup create job queued", operation, job_id, 0, site_id);
}

BackupJobResult BackupJobService::enqueue_restore(uint64_t backup_id, uint64_t target_site_id, const std::string& mode, const std::string& confirmation) {
    const std::string operation = "restore";
    auto locks = locks_;
    if (!acquire(locks, backup_id, operation)) return result(false, "operation_already_running", "A backup restore operation is already queued", operation, 0, backup_id, target_site_id);
    const uint64_t job_id = jobs_.create("backup-restore", restore_steps());
    jobs_.update(job_id, "pending", 0, "Backup restore queued");
    auto* service = &backup_service_;
    auto persist = persist_;
    const bool submitted = executor_.submit(job_id, [locks, service, persist, backup_id, target_site_id, mode, confirmation](jobs::JobManager& jobs, uint64_t queued_job_id) {
        struct Release {
            std::shared_ptr<QueueLockState> locks;
            uint64_t id;
            std::string operation;
            ~Release() { BackupJobService::release(locks, id, operation); }
        } release{locks, backup_id, "restore"};
        jobs.update(queued_job_id, "running", 10, "Backup restore running");
        auto op_result = service->restore_backup(backup_id, target_site_id, mode, confirmation, queued_job_id);
        if (persist) (void)persist();
        jobs.update_step_details(queued_job_id, op_result.steps);
        jobs.update_failure(queued_job_id, op_result.failure);
        jobs.update(queued_job_id, op_result.success ? "completed" : "failed", 100, op_result.message);
    });
    if (!submitted) {
        release(locks, backup_id, operation);
        jobs_.update(job_id, "failed", 0, "Task queue unavailable");
        return result(false, "queue_unavailable", "Task queue unavailable", operation, job_id, backup_id, target_site_id);
    }
    return result(true, "job_queued", "Backup restore job queued", operation, job_id, backup_id, target_site_id);
}

} // namespace containercp::backup
