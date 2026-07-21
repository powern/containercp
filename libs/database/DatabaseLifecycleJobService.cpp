#include "database/DatabaseLifecycleJobService.h"

#include "database/DatabaseIdentifierValidator.h"
#include "database/DatabaseLifecycleAudit.h"

#include <utility>
#include <vector>

namespace containercp::database {
namespace {

DatabaseLifecycleJobResult job_result(bool success,
                                      std::string code,
                                      std::string message,
                                      std::string operation,
                                      uint64_t job_id = 0,
                                      uint64_t database_id = 0,
                                      uint64_t site_id = 0) {
    return {success, std::move(code), std::move(message), job_id, database_id, site_id, std::move(operation)};
}

std::vector<std::string> lifecycle_job_steps() {
    return {
        "Validating ownership",
        "Checking MariaDB runtime",
        "Preparing secure credentials",
        "Checking physical state",
        "Creating database",
        "Creating managed user",
        "Applying grants",
        "Verifying connection",
        "Persisting metadata",
        "Cleaning temporary credentials",
        "Compensating changes",
        "Completed",
        "Manual recovery required",
    };
}

void audit_job(const std::string& operation,
               const std::string& stage,
               const std::string& result,
               const std::string& code,
               uint64_t job_id,
               uint64_t site_id,
               uint64_t database_id,
               const std::string& domain,
               DatabaseLifecycleAuditEvent::Level level = DatabaseLifecycleAuditEvent::Level::Info) {
    DatabaseLifecycleAuditLogger::log({operation, stage, result, code, job_id, site_id, database_id, domain, false, level});
}

bool safe_recovery_confirmation(const std::string& confirmation, const std::string& database_name, const std::string& domain) {
    return database_drop_confirmation_valid(confirmation, database_name, domain);
}

} // namespace

DatabaseLifecycleJobService::DatabaseLifecycleJobService(site::SiteManager& sites,
                                                         DatabaseManager& databases,
                                                         jobs::JobManager& jobs,
                                                         jobs::JobExecutor& executor,
                                                         DatabaseLifecycleService& lifecycle,
                                                         PasswordGenerator password_generator,
                                                         PersistCallback persist)
    : sites_(sites)
    , databases_(databases)
    , jobs_(jobs)
    , executor_(executor)
    , lifecycle_(lifecycle)
    , password_generator_(std::move(password_generator))
    , persist_(std::move(persist))
    , locks_(std::make_shared<QueueLockState>()) {
}

std::string DatabaseLifecycleJobService::lock_key(uint64_t site_id, uint64_t database_id, const std::string& operation) {
    return std::to_string(site_id) + ":" + std::to_string(database_id) + ":" + operation;
}

bool DatabaseLifecycleJobService::acquire(const std::shared_ptr<QueueLockState>& state,
                                          uint64_t site_id,
                                          uint64_t database_id,
                                          const std::string& operation) {
    std::lock_guard<std::mutex> guard(state->mutex);
    return state->locks.insert(lock_key(site_id, database_id, operation)).second;
}

void DatabaseLifecycleJobService::release(const std::shared_ptr<QueueLockState>& state,
                                          uint64_t site_id,
                                          uint64_t database_id,
                                          const std::string& operation) {
    std::lock_guard<std::mutex> guard(state->mutex);
    state->locks.erase(lock_key(site_id, database_id, operation));
}

DatabaseLifecycleJobResult DatabaseLifecycleJobService::enqueue_create(uint64_t site_id,
                                                                       const std::string& database_name,
                                                                       const std::string& database_user) {
    auto* site_record = sites_.find_by_id(site_id);
    if (site_record == nullptr) {
        return job_result(false, "site_not_found", "Site was not found", "create", 0, 0, site_id);
    }
    for (const auto& database : databases_.list()) {
        if (database.site_id == site_id && database.enabled) {
            return job_result(false, "database_already_exists", "Site already has its managed application database", "create", 0, database.id, site_id);
        }
    }
    const auto db_validation = DatabaseIdentifierValidator::validate_database_name(database_name);
    const auto user_validation = DatabaseIdentifierValidator::validate_user_name(database_user);
    if (!db_validation.valid) return job_result(false, db_validation.code, db_validation.message, "create", 0, 0, site_id);
    if (!user_validation.valid) return job_result(false, user_validation.code, user_validation.message, "create", 0, 0, site_id);

    const uint64_t database_id = databases_.create(database_name, database_user, password_generator_(), 0, site_id);
    if (!persist_()) {
        databases_.remove(database_id);
        return job_result(false, "metadata_persist_failed", "Database metadata could not be prepared", "create", 0, database_id, site_id);
    }
    return enqueue_existing("create", site_id, database_id);
}

DatabaseLifecycleJobResult DatabaseLifecycleJobService::enqueue_verify(uint64_t database_id) {
    auto* database = databases_.find(database_id);
    if (database == nullptr) {
        return job_result(false, "database_not_found", "Database was not found", "verify", 0, database_id, 0);
    }
    return enqueue_existing("verify", database->site_id, database_id);
}

DatabaseLifecycleJobResult DatabaseLifecycleJobService::enqueue_drop(uint64_t database_id, const std::string& confirmation) {
    auto* database = databases_.find(database_id);
    if (database == nullptr) {
        return job_result(false, "database_not_found", "Database was not found", "drop", 0, database_id, 0);
    }
    auto* site_record = sites_.find_by_id(database->site_id);
    if (site_record == nullptr) {
        return job_result(false, "site_not_found", "Site was not found", "drop", 0, database_id, database->site_id);
    }
    if (!database_drop_confirmation_valid(confirmation, database->db_name, site_record->domain)) {
        audit_job("drop", "confirmation", "rejected", "confirmation_mismatch", 0, database->site_id, database_id, site_record->domain, DatabaseLifecycleAuditEvent::Level::Warning);
        return job_result(false, "confirmation_mismatch", "Confirmation must match the database name or site domain", "drop", 0, database_id, database->site_id);
    }
    if (!lifecycle_.can_drop(*database)) {
        return job_result(false, lifecycle_.drop_block_reason(*database), "Database cannot be physically dropped", "drop", 0, database_id, database->site_id);
    }
    return enqueue_existing("drop", database->site_id, database_id, confirmation);
}

DatabaseLifecycleJobResult DatabaseLifecycleJobService::forget_metadata(uint64_t database_id, const std::string& confirmation) {
    auto* database = databases_.find(database_id);
    if (database == nullptr) {
        return job_result(false, "database_not_found", "Database was not found", "forget-metadata", 0, database_id, 0);
    }
    auto* site_record = sites_.find_by_id(database->site_id);
    if (site_record == nullptr) {
        return job_result(false, "site_not_found", "Site was not found", "forget-metadata", 0, database_id, database->site_id);
    }
    if (!safe_recovery_confirmation(confirmation, database->db_name, site_record->domain)) {
        return job_result(false, "confirmation_mismatch", "Confirmation must match the database name or site domain", "forget-metadata", 0, database_id, database->site_id);
    }
    audit_job("forget-metadata", "requested", "accepted", {}, 0, database->site_id, database_id, site_record->domain, DatabaseLifecycleAuditEvent::Level::Warning);
    databases_.remove(database_id);
    if (!persist_()) {
        return job_result(false, "metadata_persist_failed", "Metadata-only recovery failed", "forget-metadata", 0, database_id, site_record->id);
    }
    audit_job("forget-metadata", "metadata_removed", "success", {}, 0, site_record->id, database_id, site_record->domain, DatabaseLifecycleAuditEvent::Level::Warning);
    return job_result(true, "metadata_removed", "Database metadata removed; physical MariaDB objects were not dropped", "forget-metadata", 0, database_id, site_record->id);
}

DatabaseLifecycleJobResult DatabaseLifecycleJobService::enqueue_existing(const std::string& operation,
                                                                        uint64_t site_id,
                                                                        uint64_t database_id,
                                                                        const std::string& confirmation) {
    auto* site_record = sites_.find_by_id(site_id);
    auto* database = databases_.find(database_id);
    if (site_record == nullptr || database == nullptr || database->site_id != site_id) {
        return job_result(false, "target_not_found", "Database target was not found", operation, 0, database_id, site_id);
    }
    auto locks = locks_;
    if (!acquire(locks, site_id, database_id, operation)) {
        return job_result(false, "operation_already_running", "A database lifecycle operation is already queued", operation, 0, database_id, site_id);
    }

    const uint64_t job_id = jobs_.create("database-" + operation, lifecycle_job_steps());
    jobs_.update(job_id, "pending", 0, "Database " + operation + " queued");
    audit_job(operation, "requested", "received", {}, job_id, site_id, database_id, site_record->domain);

    const std::string domain = site_record->domain;
    const std::string database_name = database->db_name;
    auto* lifecycle = &lifecycle_;
    const bool submitted = executor_.submit(job_id, [locks, lifecycle, operation, site_id, database_id, confirmation, domain, database_name](jobs::JobManager& jobs, uint64_t queued_job_id) {
        struct Release {
            std::shared_ptr<QueueLockState> locks;
            uint64_t site_id;
            uint64_t database_id;
            std::string operation;
            ~Release() { DatabaseLifecycleJobService::release(locks, site_id, database_id, operation); }
        } release{locks, site_id, database_id, operation};

        DatabaseLifecycleResult result;
        jobs.update(queued_job_id, "running", 10, "Database " + operation + " running");
        if (operation == "create") {
            result = lifecycle->createManagedDatabase({site_id, database_id, queued_job_id});
        } else if (operation == "verify") {
            result = lifecycle->verifyManagedDatabase({site_id, database_id, queued_job_id});
        } else {
            result = lifecycle->dropManagedDatabase({site_id, database_id, queued_job_id, confirmation, domain, database_name});
        }
        jobs.update_step_details(queued_job_id, result.steps);
        jobs.update_failure(queued_job_id, result.failure);
        jobs.update(queued_job_id, result.success ? "completed" : "failed", 100, result.message);
    });
    if (!submitted) {
        release(locks, site_id, database_id, operation);
        jobs_.update(job_id, "failed", 0, "Task queue unavailable");
        if (operation == "create") {
            databases_.remove(database_id);
            (void)persist_();
        }
        return job_result(false, "queue_unavailable", "Task queue unavailable", operation, job_id, database_id, site_id);
    }
    return job_result(true, "job_queued", "Database lifecycle job queued", operation, job_id, database_id, site_id);
}

} // namespace containercp::database
