#ifndef CONTAINERCP_RUNTIME_SITE_RUNTIME_MANAGER_H
#define CONTAINERCP_RUNTIME_SITE_RUNTIME_MANAGER_H

#include "core/OperationResult.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::runtime {

struct ContainerStatus {
    std::string name;
    std::string status;   // Running, Stopped, Unhealthy, Starting, Unknown, Error
    std::string health;   // healthy, unhealthy, starting, (empty)
};

struct SiteRuntimeStatus {
    ContainerStatus web;
    ContainerStatus php;
};

class SiteRuntimeManager {
public:
    static const std::vector<std::string>& valid_actions();

    SiteRuntimeManager(logger::Logger& logger,
                       const std::string& sites_root);

    SiteRuntimeStatus get_status(uint64_t site_id, const std::string& domain) const;
    std::string container_status(const std::string& compose_dir,
                                 const std::string& service) const;

    // Validate and prepare a runtime action for future execution.
    // Phase 2: stub — validates action, returns success.
    // Phase 3: will execute docker compose restart via JobExecutor.
    core::OperationResult execute_action(uint64_t site_id,
                                          const std::string& domain,
                                          const std::string& action) const;

private:
    static std::string path_join(const std::string& a, const std::string& b);

    logger::Logger& logger_;
    std::string sites_root_;
    CommandExecutor executor_;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_SITE_RUNTIME_MANAGER_H
