#ifndef CONTAINERCP_SSL_RENEWAL_SCHEDULER_H
#define CONTAINERCP_SSL_RENEWAL_SCHEDULER_H

#include "ssl/CertificateStore.h"
#include "ssl/CertificateProvider.h"
#include "ssl/SslCertificateManager.h"
#include "jobs/JobExecutor.h"
#include "jobs/JobManager.h"
#include "logger/Logger.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

namespace containercp::ssl {

class RenewalScheduler {
public:
    RenewalScheduler(logger::Logger& logger,
                     CertificateStore& store,
                     jobs::JobManager& job_manager,
                     jobs::JobExecutor& executor,
                     std::unordered_map<std::string, std::shared_ptr<CertificateProvider>>& providers);

    ~RenewalScheduler();

    void start();
    void shutdown();

    void set_interval_hours(int hours);

    // Next check time, for API status
    std::string next_check_at() const;

    // Forced immediate check (used in tests)
    void check_now();

private:
    void scheduler_loop();
    int backoff_seconds(int attempt) const;

    logger::Logger& logger_;
    CertificateStore& store_;
    jobs::JobManager& job_manager_;
    jobs::JobExecutor& executor_;
    std::unordered_map<std::string, std::shared_ptr<CertificateProvider>>& providers_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::chrono::seconds interval_{24 * 3600};

    std::string next_check_at_;
};

} // namespace containercp::ssl

#endif // CONTAINERCP_SSL_RENEWAL_SCHEDULER_H
