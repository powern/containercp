#ifndef CONTAINERCP_CORE_RECOVERY_MANAGER_H
#define CONTAINERCP_CORE_RECOVERY_MANAGER_H

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
};

} // namespace containercp::core

#endif // CONTAINERCP_CORE_RECOVERY_MANAGER_H
