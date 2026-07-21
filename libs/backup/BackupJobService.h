#ifndef CONTAINERCP_BACKUP_BACKUP_JOB_SERVICE_H
#define CONTAINERCP_BACKUP_BACKUP_JOB_SERVICE_H

#include "backup/BackupService.h"
#include "jobs/JobExecutor.h"
#include "jobs/JobManager.h"

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace containercp::backup {

struct BackupJobResult {
    bool success = false;
    std::string code;
    std::string message;
    uint64_t job_id = 0;
    uint64_t backup_id = 0;
    uint64_t site_id = 0;
    uint64_t recovery_backup_id = 0;
    std::string operation;
};

class BackupJobService {
public:
    using PersistCallback = std::function<bool()>;

    BackupJobService(jobs::JobManager& jobs,
                     jobs::JobExecutor& executor,
                     BackupService& backup_service,
                     PersistCallback persist);

    BackupJobResult enqueue_create(uint64_t site_id);
    BackupJobResult enqueue_restore(uint64_t backup_id, uint64_t target_site_id, const std::string& mode, const std::string& confirmation);

private:
    struct QueueLockState {
        std::mutex mutex;
        std::set<std::string> locks;
    };

    static std::string lock_key(uint64_t id, const std::string& operation);
    static bool acquire(const std::shared_ptr<QueueLockState>& state, uint64_t id, const std::string& operation);
    static void release(const std::shared_ptr<QueueLockState>& state, uint64_t id, const std::string& operation);

    jobs::JobManager& jobs_;
    jobs::JobExecutor& executor_;
    BackupService& backup_service_;
    PersistCallback persist_;
    std::shared_ptr<QueueLockState> locks_;
};

} // namespace containercp::backup

#endif // CONTAINERCP_BACKUP_BACKUP_JOB_SERVICE_H
