#include "RecoveryManager.h"
#include "core/ServiceRegistry.h"
#include "proxy/NginxProxyProvider.h"

#include <chrono>
#include <ctime>
#include <thread>

namespace containercp::core {

RecoveryManager::RecoveryManager(logger::Logger& logger,
                                 proxy::NginxProxyProvider& proxy_provider,
                                 ServiceRegistry& services)
    : logger_(logger)
    , proxy_provider_(proxy_provider)
    , services_(services)
{
}

RecoveryManager::~RecoveryManager() {
    shutdown();
}

void RecoveryManager::start() {
    if (running_.exchange(true)) return;
    logger_.info("RECOVERY", "RecoveryManager started (check every "
                 + std::to_string(CHECK_INTERVAL_SEC) + "s, max "
                 + std::to_string(MAX_RETRIES) + " retries, "
                 + std::to_string(COOLDOWN_SEC) + "s cooldown)");
    thread_ = std::thread([this]() { check_loop(); });
}

void RecoveryManager::shutdown() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool RecoveryManager::is_proxy_healthy() {
    return proxy_provider_.central_proxy_running();
}

void RecoveryManager::check_loop() {
    logger_.info("RECOVERY", "check_loop entered");
    while (running_) {
        logger_.info("RECOVERY", "Health check tick (fail_count="
                     + std::to_string(fail_count_) + ")");
        bool healthy = is_proxy_healthy();
        logger_.info("RECOVERY", "central_proxy_running() returned "
                     + std::string(healthy ? "true" : "false")
                     + " for container containercp-proxy");
        if (!running_) break;

        if (healthy) {
            if (fail_count_ > 0) {
                logger_.info("RECOVERY", "Proxy healthy again, resetting failure counter");
                fail_count_ = 0;
            }
        } else {
            logger_.warning("RECOVERY", "Proxy unhealthy (fail_count="
                           + std::to_string(fail_count_ + 1) + ")");

            if (fail_count_ >= MAX_RETRIES) {
                logger_.error("RECOVERY",
                    "Proxy recovery failed " + std::to_string(MAX_RETRIES)
                    + " times. Entering cooldown for " + std::to_string(COOLDOWN_SEC) + "s. "
                    + "Manual intervention may be required. "
                    + "Recovery commands: systemctl restart containercpd, "
                    + "docker rm -f containercp-proxy && systemctl restart containercpd");
                std::this_thread::sleep_for(std::chrono::seconds(COOLDOWN_SEC));
                fail_count_ = 0;
            } else {
                // Check if manual recovery is in progress — don't overlap
                {
                    std::lock_guard<std::mutex> lock(status_mutex_);
                    if (recovery_active_) {
                        logger_.info("RECOVERY", "Recovery already in progress, skipping background attempt");
                        continue;  // skip sleep, re-check immediately
                    }
                    recovery_active_ = true;
                }
                fail_count_++;
                logger_.info("RECOVERY", "Recovery attempt " + std::to_string(fail_count_)
                             + "/" + std::to_string(MAX_RETRIES));
                recover();
                {
                    std::lock_guard<std::mutex> lock(status_mutex_);
                    recovery_active_ = false;
                }
            }
        }

        // Sleep before next health check (interruptible on shutdown)
        logger_.info("RECOVERY", "Sleeping " + std::to_string(CHECK_INTERVAL_SEC) + "s until next check");
        for (int i = 0; i < CHECK_INTERVAL_SEC && running_; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    logger_.info("RECOVERY", "check_loop exiting");
}

RecoveryStatus RecoveryManager::status() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    RecoveryStatus s;
    s.manager_running = running_.load();
    s.recovery_in_progress = recovery_active_;
    s.last_recovery_at = last_recovery_time_;
    s.last_recovery_result = last_recovery_result_;
    return s;
}

core::OperationResult RecoveryManager::recover_now() {
    logger_.info("RECOVERY", "Manual recovery requested via API");
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        if (recovery_active_) {
            return {false, "Recovery already in progress"};
        }
        recovery_active_ = true;
    }
    recover();
    {
        std::lock_guard<std::mutex> lock(status_mutex_);
        recovery_active_ = false;
        last_recovery_time_ = std::time(nullptr);
        if (is_proxy_healthy()) {
            last_recovery_result_ = "success";
            return {true, "Recovery completed successfully"};
        }
        last_recovery_result_ = "failed";
        return {false, "Recovery completed but proxy is still unhealthy"};
    }
}

void RecoveryManager::recover() {
    // Step 1: Recreate/restart the proxy container
    logger_.info("RECOVERY", "Step 1/3: ensure_central_proxy()");
    auto proxy_result = proxy_provider_.ensure_central_proxy();
    if (!proxy_result.success) {
        logger_.error("RECOVERY", "ensure_central_proxy failed: " + proxy_result.message);
    } else {
        logger_.info("RECOVERY", "ensure_central_proxy OK");
    }

    // Step 2: Regenerate admin proxy config and re-attach SSL
    logger_.info("RECOVERY", "Step 2/3: ensure_admin_proxy()");
    auto admin_result = services_.ensure_admin_proxy();
    if (!admin_result.success) {
        logger_.warning("RECOVERY", "ensure_admin_proxy: " + admin_result.message
                       + " (server_hostname may not be configured)");
    } else {
        logger_.info("RECOVERY", "ensure_admin_proxy OK");
    }

    // Step 3: Resync HTTPS configs for all sites
    logger_.info("RECOVERY", "Step 3/3: sync_all_https_configs()");
    services_.sync_all_https_configs();
    logger_.info("RECOVERY", "sync_all_https_configs completed");

    // Final check
    if (is_proxy_healthy()) {
        logger_.info("RECOVERY", "Recovery successful");
        fail_count_ = 0;
    } else {
        logger_.error("RECOVERY", "Recovery attempt " + std::to_string(fail_count_)
                      + " failed — proxy still unhealthy");
    }
}

} // namespace containercp::core
