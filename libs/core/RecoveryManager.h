#ifndef CONTAINERCP_CORE_RECOVERY_MANAGER_H
#define CONTAINERCP_CORE_RECOVERY_MANAGER_H

#include "core/OperationResult.h"
#include "logger/Logger.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace containercp::proxy {
class NginxProxyProvider;
}

namespace containercp::core {
class ServiceRegistry;

// Background health monitor for the central reverse proxy.
//
// Detects when containercp-proxy is unhealthy and attempts self-healing
// by reusing existing public methods — never duplicates Bootstrap or
// proxy creation logic.
//
// Retry policy:
//   - Check every 60 seconds
//   - Max 3 consecutive failed recoveries before 300s cooldown
//   - Failure counter resets on any successful check
class RecoveryManager {
public:
    RecoveryManager(logger::Logger& logger,
                    proxy::NginxProxyProvider& proxy_provider,
                    ServiceRegistry& services);

    ~RecoveryManager();

    void start();
    void shutdown();

    // Run recovery synchronously (for API calls).  Returns success/failure.
    // Thread-safe: uses the same recover() internals as the background loop.
    core::OperationResult recover_now();

    // Recovery state (best-effort, reset on daemon restart)
    bool recovery_in_progress() const { return recovery_active_; }
    std::time_t last_recovery_at() const { return last_recovery_time_; }
    std::string last_recovery_result() const { return last_recovery_result_; }

private:
    void check_loop();
    bool is_proxy_healthy();
    void recover();

    logger::Logger& logger_;
    proxy::NginxProxyProvider& proxy_provider_;
    ServiceRegistry& services_;

    std::thread thread_;
    std::atomic<bool> running_{false};

    static constexpr int CHECK_INTERVAL_SEC = 60;
    static constexpr int MAX_RETRIES = 3;
    static constexpr int COOLDOWN_SEC = 300;

    int fail_count_ = 0;
    std::atomic<bool> recovery_active_{false};
    std::time_t last_recovery_time_ = 0;
    std::string last_recovery_result_;
};

} // namespace containercp::core

#endif // CONTAINERCP_CORE_RECOVERY_MANAGER_H
