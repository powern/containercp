#ifndef CONTAINERCP_RUNTIME_SITE_RUNTIME_MANAGER_H
#define CONTAINERCP_RUNTIME_SITE_RUNTIME_MANAGER_H

#include "core/OperationResult.h"
#include "logger/Logger.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::runtime {

struct ContainerStatus {
    std::string name;
    std::string status;  // Running, Stopped, Unhealthy, Unknown
    std::string health;  // healthy, unhealthy, starting, (empty)
    std::string uptime;
    std::string image;
};

struct SiteRuntimeStatus {
    ContainerStatus web;
    ContainerStatus php;
    std::string https_status;  // Valid, Expired, Disabled, Error
};

class SiteRuntimeManager {
public:
    SiteRuntimeManager(logger::Logger& logger, const std::string& sites_root);

    SiteRuntimeStatus get_status(uint64_t site_id) const;

    std::string container_status(const std::string& project, const std::string& service) const;

private:
    logger::Logger& logger_;
    std::string sites_root_;
};

} // namespace containercp::runtime

#endif // CONTAINERCP_RUNTIME_SITE_RUNTIME_MANAGER_H
