#ifndef CONTAINERCP_RUNTIME_SITE_RUNTIME_MANAGER_H
#define CONTAINERCP_RUNTIME_SITE_RUNTIME_MANAGER_H

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
    std::string https_status;  // Active, Expiring, Expired, Disabled, Error
};

class SiteRuntimeManager {
public:
    SiteRuntimeManager(logger::Logger& logger,
                       const std::string& sites_root,
                       const std::string& ssl_root);

    SiteRuntimeStatus get_status(uint64_t site_id, const std::string& domain) const;
    std::string container_status(const std::string& compose_dir,
                                 const std::string& service) const;

    static std::string https_status_from_metadata(const std::string& ssl_root,
                                                  uint64_t site_id);

private:
    static std::string path_join(const std::string& a, const std::string& b);

    logger::Logger& logger_;
    std::string sites_root_;
    std::string ssl_root_;
    CommandExecutor executor_;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_SITE_RUNTIME_MANAGER_H
