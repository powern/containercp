#ifndef CONTAINERCP_DATABASE_DATABASE_LIFECYCLE_JOB_SERVICE_H
#define CONTAINERCP_DATABASE_DATABASE_LIFECYCLE_JOB_SERVICE_H

#include "database/DatabaseLifecycleService.h"
#include "jobs/JobExecutor.h"
#include "jobs/JobManager.h"
#include "site/SiteManager.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace containercp::database {

struct DatabaseLifecycleJobResult {
    bool success = false;
    std::string code;
    std::string message;
    uint64_t job_id = 0;
    uint64_t database_id = 0;
    uint64_t site_id = 0;
    std::string operation;
};

class DatabaseLifecycleJobService {
public:
    using PasswordGenerator = std::function<std::string()>;
    using PersistCallback = std::function<bool()>;

    DatabaseLifecycleJobService(site::SiteManager& sites,
                                DatabaseManager& databases,
                                jobs::JobManager& jobs,
                                jobs::JobExecutor& executor,
                                DatabaseLifecycleService& lifecycle,
                                PasswordGenerator password_generator,
                                PersistCallback persist);

    DatabaseLifecycleJobResult enqueue_create(uint64_t site_id,
                                              const std::string& database_name,
                                              const std::string& database_user);
    DatabaseLifecycleJobResult enqueue_verify(uint64_t database_id);
    DatabaseLifecycleJobResult enqueue_drop(uint64_t database_id, const std::string& confirmation);
    DatabaseLifecycleJobResult forget_metadata(uint64_t database_id, const std::string& confirmation);

private:
    struct QueueLockState {
        std::mutex mutex;
        std::set<std::string> locks;
    };

    static std::string lock_key(uint64_t site_id, uint64_t database_id, const std::string& operation);
    static bool acquire(const std::shared_ptr<QueueLockState>& state, uint64_t site_id, uint64_t database_id, const std::string& operation);
    static void release(const std::shared_ptr<QueueLockState>& state, uint64_t site_id, uint64_t database_id, const std::string& operation);
    DatabaseLifecycleJobResult enqueue_existing(const std::string& operation,
                                                uint64_t site_id,
                                                uint64_t database_id,
                                                const std::string& confirmation = {});

    site::SiteManager& sites_;
    DatabaseManager& databases_;
    jobs::JobManager& jobs_;
    jobs::JobExecutor& executor_;
    DatabaseLifecycleService& lifecycle_;
    PasswordGenerator password_generator_;
    PersistCallback persist_;
    std::shared_ptr<QueueLockState> locks_;
};

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_LIFECYCLE_JOB_SERVICE_H
