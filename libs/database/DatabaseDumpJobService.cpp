#include "database/DatabaseDumpJobService.h"

#include <utility>
#include <vector>

namespace containercp::database {
namespace {

DatabaseDumpJobResult result(bool success,
                             std::string code,
                             std::string message,
                             std::string operation,
                             uint64_t job_id = 0,
                             uint64_t database_id = 0,
                             uint64_t site_id = 0,
                             std::string artifact_id = {}) {
    return {success, std::move(code), std::move(message), job_id, database_id, site_id, std::move(operation), std::move(artifact_id)};
}

std::vector<std::string> job_steps(const std::string& operation) {
    if (operation == "export") {
        return {"Validating database ownership", "Checking MariaDB runtime", "Preparing secure credentials",
                "Preparing export staging", "Creating logical SQL dump", "Validating export artifact",
                "Calculating artifact metadata", "Finalizing artifact", "Completed", "Cleaning partial artifact on failure"};
    }
    return {"Validating database ownership", "Validating import artifact", "Checking MariaDB runtime",
            "Preparing secure credentials", "Preparing recovery export if required", "Importing SQL",
            "Verifying database access", "Cleaning staging", "Completed", "Manual recovery required"};
}

} // namespace

DatabaseDumpJobService::DatabaseDumpJobService(DatabaseManager& databases,
                                               jobs::JobManager& jobs,
                                               jobs::JobExecutor& executor,
                                               DatabaseDumpService& dump_service)
    : databases_(databases)
    , jobs_(jobs)
    , executor_(executor)
    , dump_service_(dump_service)
    , locks_(std::make_shared<QueueLockState>()) {
}

std::string DatabaseDumpJobService::lock_key(uint64_t database_id, const std::string& operation) {
    return std::to_string(database_id) + ":" + operation;
}

bool DatabaseDumpJobService::acquire(const std::shared_ptr<QueueLockState>& state, uint64_t database_id, const std::string& operation) {
    std::lock_guard<std::mutex> guard(state->mutex);
    return state->locks.insert(lock_key(database_id, operation)).second;
}

void DatabaseDumpJobService::release(const std::shared_ptr<QueueLockState>& state, uint64_t database_id, const std::string& operation) {
    std::lock_guard<std::mutex> guard(state->mutex);
    state->locks.erase(lock_key(database_id, operation));
}

DatabaseDumpJobResult DatabaseDumpJobService::enqueue_export(uint64_t database_id) {
    auto* database = databases_.find(database_id);
    if (database == nullptr) return result(false, "database_not_found", "Database was not found", "export", 0, database_id);
    return enqueue("export", database_id, database_generate_artifact_id());
}

DatabaseDumpJobResult DatabaseDumpJobService::enqueue_import(uint64_t database_id, const std::string& artifact_id, const std::string& confirmation) {
    auto* database = databases_.find(database_id);
    if (database == nullptr) return result(false, "database_not_found", "Database was not found", "import", 0, database_id);
    if (!database_artifact_id_valid(artifact_id)) return result(false, "artifact_id_invalid", "Artifact identifier is invalid", "import", 0, database_id, database->site_id);
    return enqueue("import", database_id, artifact_id, confirmation);
}

DatabaseDumpJobResult DatabaseDumpJobService::enqueue(const std::string& operation,
                                                       uint64_t database_id,
                                                       const std::string& artifact_id,
                                                       const std::string& confirmation) {
    auto* database = databases_.find(database_id);
    if (database == nullptr) return result(false, "database_not_found", "Database was not found", operation, 0, database_id);
    if (!dump_service_.can_transfer(*database)) return result(false, dump_service_.transfer_block_reason(*database), "Database is not eligible for DB-4 transfer", operation, 0, database_id, database->site_id, artifact_id);
    auto locks = locks_;
    if (!acquire(locks, database_id, operation)) return result(false, "operation_already_running", "A database transfer operation is already queued", operation, 0, database_id, database->site_id, artifact_id);
    const uint64_t job_id = jobs_.create("database-" + operation, job_steps(operation));
    jobs_.update(job_id, "pending", 0, "Database " + operation + " queued");
    auto* service = &dump_service_;
    const bool submitted = executor_.submit(job_id, [locks, service, operation, database_id, artifact_id, confirmation](jobs::JobManager& jobs, uint64_t queued_job_id) {
        struct Release {
            std::shared_ptr<QueueLockState> locks;
            uint64_t database_id;
            std::string operation;
            ~Release() { DatabaseDumpJobService::release(locks, database_id, operation); }
        } release{locks, database_id, operation};
        jobs.update(queued_job_id, "running", 10, "Database " + operation + " running");
        DatabaseDumpResult op_result = operation == "export"
            ? service->exportManagedDatabase(database_id, queued_job_id, artifact_id)
            : service->importManagedDatabase(database_id, queued_job_id, artifact_id, confirmation);
        jobs.update_step_details(queued_job_id, op_result.steps);
        jobs.update_failure(queued_job_id, op_result.failure);
        jobs.update(queued_job_id, op_result.success ? "completed" : "failed", 100, op_result.message);
    });
    if (!submitted) {
        release(locks, database_id, operation);
        jobs_.update(job_id, "failed", 0, "Task queue unavailable");
        return result(false, "queue_unavailable", "Task queue unavailable", operation, job_id, database_id, database->site_id, artifact_id);
    }
    return result(true, "job_queued", "Database transfer job queued", operation, job_id, database_id, database->site_id, artifact_id);
}

} // namespace containercp::database
