#ifndef CONTAINERCP_RUNTIME_HEALTH_REPORT_H
#define CONTAINERCP_RUNTIME_HEALTH_REPORT_H

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace containercp::runtime {

// Status of a single service within a module's health report.
//   status:  "ok", "degraded", "error", "unknown"
//   message: human-readable detail (e.g. "running", "container not found")
struct ServiceHealth {
    std::string name;
    std::string status = "unknown";
    std::string message;
};

// Per-module health report.  Designed to evolve without breaking
// existing callers — new fields are added as module-specific details
// evolve, not as top-level fields.
struct HealthReport {
    std::string status = "unknown";  // aggregate: "ok", "degraded", "error"
    std::vector<ServiceHealth> services;

    // Arbitrary JSON object with module-specific operational data.
    // Left empty for modules that have no additional detail.
    // Example: {"domain_count":5, "mailbox_count":12}
    std::string details;  // JSON object, or empty
};

// Generic health registry.  Modules register health check callbacks
// during ServiceRegistry construction.  The /api/health endpoint
// collects all reports and aggregates them.
class HealthRegistry {
public:
    using HealthCheck = std::function<HealthReport()>;

    void register_check(const std::string& name, HealthCheck check);

    // Returns all registered checks as (name, report) pairs.
    std::vector<std::pair<std::string, HealthReport>> check_all();

    // Run a single named check.
    HealthReport check(const std::string& name);

private:
    std::map<std::string, HealthCheck> checks_;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_HEALTH_REPORT_H
