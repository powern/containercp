#ifndef CONTAINERCP_DATABASE_DATABASE_DUMP_JOB_SERVICE_H
#define CONTAINERCP_DATABASE_DATABASE_DUMP_JOB_SERVICE_H

#include "database/DatabaseDumpService.h"
#include "jobs/JobExecutor.h"
#include "jobs/JobManager.h"

#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace containercp::database {

struct DatabaseDumpJobResult {
    bool success = false;
    std::string code;
    std::string message;
    uint64_t job_id = 0;
    uint64_t database_id = 0;
    uint64_t site_id = 0;
    std::string operation;
    std::string artifact_id;
};

class DatabaseDumpJobService {
public:
    DatabaseDumpJobService(DatabaseManager& databases,
                           jobs::JobManager& jobs,
                           jobs::JobExecutor& executor,
                           DatabaseDumpService& dump_service);

    DatabaseDumpJobResult enqueue_export(uint64_t database_id);
    DatabaseDumpJobResult enqueue_import(uint64_t database_id, const std::string& artifact_id, const std::string& confirmation);

private:
    struct QueueLockState {
        std::mutex mutex;
        std::set<std::string> locks;
    };

    static std::string lock_key(uint64_t database_id, const std::string& operation);
    static bool acquire(const std::shared_ptr<QueueLockState>& state, uint64_t database_id, const std::string& operation);
    static void release(const std::shared_ptr<QueueLockState>& state, uint64_t database_id, const std::string& operation);
    DatabaseDumpJobResult enqueue(const std::string& operation,
                                  uint64_t database_id,
                                  const std::string& artifact_id,
                                  const std::string& confirmation = {});

    DatabaseManager& databases_;
    jobs::JobManager& jobs_;
    jobs::JobExecutor& executor_;
    DatabaseDumpService& dump_service_;
    std::shared_ptr<QueueLockState> locks_;
};

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_DUMP_JOB_SERVICE_H
