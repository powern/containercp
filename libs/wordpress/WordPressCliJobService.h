#ifndef CONTAINERCP_WORDPRESS_WORDPRESS_CLI_JOB_SERVICE_H
#define CONTAINERCP_WORDPRESS_WORDPRESS_CLI_JOB_SERVICE_H

#include "jobs/JobExecutor.h"
#include "jobs/JobManager.h"
#include "wordpress/WordPressCliService.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <string>

namespace containercp::wordpress {

struct WordPressCliMutationJobResult {
    bool accepted = false;
    std::string code;
    std::string message;
    uint64_t job_id = 0;
    uint64_t site_id = 0;
    std::string operation;
    std::string package_id;
};

class WordPressCliJobService {
public:
    WordPressCliJobService(jobs::JobManager& jobs,
                           jobs::JobExecutor& executor,
                           WordPressCliService& cli,
                           logger::Logger& logger);

    WordPressCliMutationJobResult enqueue(uint64_t site_id,
                                          WordPressCliMutation mutation,
                                          const std::string& package_id,
                                          const std::string& admin_username,
                                          const std::string& domain);

private:
    struct QueueLockState {
        std::mutex mutex;
        std::set<uint64_t> site_ids;
    };

    static bool acquire(const std::shared_ptr<QueueLockState>& state, uint64_t site_id);
    static void release(const std::shared_ptr<QueueLockState>& state, uint64_t site_id);

    jobs::JobManager& jobs_;
    jobs::JobExecutor& executor_;
    WordPressCliService& cli_;
    logger::Logger& logger_;
    std::shared_ptr<QueueLockState> locks_;
};

} // namespace containercp::wordpress

#endif // CONTAINERCP_WORDPRESS_WORDPRESS_CLI_JOB_SERVICE_H
