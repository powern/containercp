#ifndef CONTAINERCP_DATABASE_DATABASE_CREDENTIAL_ROTATION_JOB_SERVICE_H
#define CONTAINERCP_DATABASE_DATABASE_CREDENTIAL_ROTATION_JOB_SERVICE_H

#include "database/DatabaseCredentialRotationService.h"
#include "database/DatabaseManager.h"
#include "jobs/JobExecutor.h"
#include "jobs/JobManager.h"
#include "site/SiteManager.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace containercp::database {

struct DatabaseCredentialRotationJobRequest {
    uint64_t site_id = 0;
    uint64_t database_id = 0;
    std::string confirmation;
};

struct DatabaseCredentialRotationJobResult {
    bool success = false;
    std::string code;
    std::string message;
    uint64_t job_id = 0;
};

class DatabaseCredentialRotationJobService {
public:
    DatabaseCredentialRotationJobService(site::SiteManager& sites,
                                         DatabaseManager& databases,
                                         jobs::JobManager& jobs,
                                         jobs::JobExecutor& executor,
                                         DatabaseCredentialRotationService& rotation);

    DatabaseCredentialRotationJobResult enqueue(const DatabaseCredentialRotationJobRequest& request);

private:
    struct QueueLockState {
        std::mutex mutex;
        std::set<std::string> queued_locks;
    };

    static std::string lock_key(uint64_t site_id, uint64_t database_id);
    static bool acquire_queue_lock(const std::shared_ptr<QueueLockState>& state, uint64_t site_id, uint64_t database_id);
    static void release_queue_lock(const std::shared_ptr<QueueLockState>& state, uint64_t site_id, uint64_t database_id);

    site::SiteManager& sites_;
    DatabaseManager& databases_;
    jobs::JobManager& jobs_;
    jobs::JobExecutor& executor_;
    DatabaseCredentialRotationService& rotation_;
    std::shared_ptr<QueueLockState> queue_locks_;
};

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_CREDENTIAL_ROTATION_JOB_SERVICE_H
