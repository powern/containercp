#ifndef CONTAINERCP_RUNTIME_SITE_RUNTIME_MANAGER_H
#define CONTAINERCP_RUNTIME_SITE_RUNTIME_MANAGER_H

#include "core/OperationResult.h"
#include "logger/Logger.h"
#include "runtime/RuntimeActionExecutor.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::runtime {

// Sites-specific bridge to the runtime subsystem.
//
// Knows WHAT services belong to a site (via ServiceRole) and delegates
// HOW (status inspection, restart execution) to RuntimeActionExecutor.
struct SiteRuntimeStatus {
    ContainerStatus web;
    ContainerStatus php;
    ContainerStatus db;
    ContainerStatus cache;
};

class SiteRuntimeManager {
public:
    static const std::vector<std::string>& valid_actions();

    SiteRuntimeManager(logger::Logger& logger,
                       const std::string& sites_root,
                       RuntimeActionExecutor& executor);

    SiteRuntimeStatus get_status(uint64_t site_id, const std::string& domain) const;

    // Map a site-level action to the compose service names it affects.
    // Delegates to ServiceRole for the canonical role→service mapping.
    // restart-web → {"web"}, restart-php → {"php"},
    // restart-db → {"mariadb"}, restart-redis → {"redis"},
    // restart-all → {} (empty = all compose services)
    // Returns empty vector for unknown actions.
    std::vector<std::string> services_for_action(const std::string& action) const;

private:
    static std::string path_join(const std::string& a, const std::string& b);

    logger::Logger& logger_;
    std::string sites_root_;
    RuntimeActionExecutor& executor_;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_SITE_RUNTIME_MANAGER_H
