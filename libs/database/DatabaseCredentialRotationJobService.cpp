#include "DatabaseCredentialRotationJobService.h"

#include <cctype>
#include <utility>
#include <vector>

namespace containercp::database {
namespace {

DatabaseCredentialRotationJobResult result(bool success, std::string code, std::string message, uint64_t job_id = 0) {
    return {success, std::move(code), std::move(message), job_id};
}

std::string job_message_for_result(const DatabaseCredentialRotationResult& rotation_result) {
    if (rotation_result.success) {
        return "Credential rotation completed";
    }
    if (rotation_result.final_state == DatabaseCredentialRotationState::Compensated) {
        return "Credential rotation failed and was rolled back";
    }
    if (rotation_result.final_state == DatabaseCredentialRotationState::ManualRecoveryRequired) {
        return "Credential rotation requires manual recovery";
    }
    return "Credential rotation failed";
}

bool safe_confirmation(const std::string& value) {
    if (value.empty() || value.size() > 253) {
        return false;
    }
    for (unsigned char c : value) {
        if (std::iscntrl(c) != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

DatabaseCredentialRotationJobService::DatabaseCredentialRotationJobService(site::SiteManager& sites,
                                                                           DatabaseManager& databases,
                                                                           jobs::JobManager& jobs,
                                                                           jobs::JobExecutor& executor,
                                                                           DatabaseCredentialRotationService& rotation)
    : sites_(sites)
    , databases_(databases)
    , jobs_(jobs)
    , executor_(executor)
    , rotation_(rotation)
    , queue_locks_(std::make_shared<QueueLockState>()) {
}

std::string DatabaseCredentialRotationJobService::lock_key(uint64_t site_id, uint64_t database_id) {
    return std::to_string(site_id) + ":" + std::to_string(database_id);
}

bool DatabaseCredentialRotationJobService::acquire_queue_lock(const std::shared_ptr<QueueLockState>& state,
                                                              uint64_t site_id,
                                                              uint64_t database_id) {
    std::lock_guard<std::mutex> guard(state->mutex);
    return state->queued_locks.insert(lock_key(site_id, database_id)).second;
}

void DatabaseCredentialRotationJobService::release_queue_lock(const std::shared_ptr<QueueLockState>& state,
                                                              uint64_t site_id,
                                                              uint64_t database_id) {
    std::lock_guard<std::mutex> guard(state->mutex);
    state->queued_locks.erase(lock_key(site_id, database_id));
}

DatabaseCredentialRotationJobResult DatabaseCredentialRotationJobService::enqueue(const DatabaseCredentialRotationJobRequest& request) {
    if (!safe_confirmation(request.confirmation)) {
        return result(false, "confirmation_invalid", "Confirmation must be a valid domain confirmation string");
    }
    auto* site = sites_.find_by_id(request.site_id);
    if (site == nullptr) {
        return result(false, "site_not_found", "Site was not found");
    }
    if (request.database_id == 0) {
        return result(false, "database_required", "Database id is required");
    }
    auto* database = databases_.find(request.database_id);
    if (database == nullptr || database->site_id != request.site_id) {
        return result(false, "database_not_found", "Database was not found for this site");
    }
    if (request.confirmation != site->domain) {
        return result(false, "confirmation_mismatch", "Confirmation must match the site domain");
    }
    auto queue_locks = queue_locks_;
    if (rotation_.is_locked(request.site_id, request.database_id) ||
        !acquire_queue_lock(queue_locks, request.site_id, request.database_id)) {
        return result(false, "rotation_already_running", "A credential rotation is already running for this site database");
    }

    const std::vector<std::string> steps = {
        "Inspecting WordPress credentials",
        "Changing database password",
        "Updating WordPress config",
        "Verifying site health",
        "Persisting metadata",
    };
    const uint64_t job_id = jobs_.create("wordpress-db-credential-rotation", steps);
    jobs_.update(job_id, "pending", 0, "Credential rotation queued");

    auto* rotation = &rotation_;
    const bool submitted = executor_.submit(job_id, [queue_locks, rotation, request](jobs::JobManager& jobs, uint64_t queued_job_id) {
        struct QueueLockRelease {
            std::shared_ptr<QueueLockState> queue_locks;
            uint64_t site_id;
            uint64_t database_id;
            ~QueueLockRelease() { DatabaseCredentialRotationJobService::release_queue_lock(queue_locks, site_id, database_id); }
        } release{queue_locks, request.site_id, request.database_id};

        try {
            jobs.update(queued_job_id, "running", 10, "Credential rotation running");
            const auto rotation_result = rotation->rotate({request.site_id, request.database_id, request.confirmation});
            if (rotation_result.success) {
                jobs.update(queued_job_id, "completed", 100, job_message_for_result(rotation_result));
            } else {
                jobs.update(queued_job_id, "failed", 100, job_message_for_result(rotation_result));
            }
        } catch (...) {
            jobs.update(queued_job_id, "failed", 100, "Credential rotation failed");
        }
    });

    if (!submitted) {
        release_queue_lock(queue_locks, request.site_id, request.database_id);
        jobs_.update(job_id, "failed", 0, "Task queue unavailable");
        return result(false, "queue_unavailable", "Task queue unavailable");
    }

    return result(true, "job_queued", "Credential rotation job queued", job_id);
}

} // namespace containercp::database
