#ifndef CONTAINERCP_NETWORK_NETWORK_SERVICE_H
#define CONTAINERCP_NETWORK_NETWORK_SERVICE_H

#include "config/Config.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

namespace containercp::network {

struct IpDetectionResult {
    std::string address;        // Detected public IP, empty if unknown
    std::string source;         // "routing_table", "external_dns", "hostname_dns_fallback", ""
    std::string detected_at;    // ISO 8601 timestamp of detection
    bool stale = false;         // true if using cached value past its TTL
    bool success = false;       // true if detection succeeded
    std::string error;          // Error message if detection failed
};

class NetworkService {
public:
    NetworkService(config::Config& config, logger::Logger& logger);

    // Get public IPv4 (cached, auto-detected)
    IpDetectionResult public_ipv4();

    // Get public IPv6 (cached, auto-detected)
    IpDetectionResult public_ipv6();

    // Force re-detection on next call
    void refresh();

    // Check if an IP address is a globally routable public address
    static bool is_public_ipv4(const std::string& ip);
    static bool is_public_ipv6(const std::string& ip);

    // Run detection synchronously (for startup)
    void detect_now();

private:
    IpDetectionResult detect_ipv4();
    IpDetectionResult detect_ipv6();

    IpDetectionResult detect_from_routing_table(bool ipv6);
    IpDetectionResult detect_from_external_dns(bool ipv6);
    IpDetectionResult detect_from_hostname_dns(bool ipv6);

    // Attempt all detection methods in order, return first success
    IpDetectionResult detect_with_fallback(bool ipv6);

    IpDetectionResult load_cached(const std::string& key);
    void save_cached(const std::string& key, const IpDetectionResult& result);

    config::Config& config_;
    logger::Logger& logger_;
    runtime::CommandExecutor executor_;

    struct CacheEntry {
        IpDetectionResult result;
        std::chrono::steady_clock::time_point timestamp;
        bool detection_in_progress = false;
    };

    mutable std::mutex cache_mutex_;
    CacheEntry cache_v4_;
    CacheEntry cache_v6_;
    std::atomic<bool> refresh_requested_{false};

    static constexpr int kCacheTTLSeconds = 300; // 5 minutes
    static constexpr int kStaleThresholdSeconds = 3600; // 1 hour — beyond this, cached values are stale
};

} // namespace containercp::network

#endif // CONTAINERCP_NETWORK_NETWORK_SERVICE_H
