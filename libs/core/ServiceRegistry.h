#ifndef CONTAINERCP_CORE_SERVICE_REGISTRY_H
#define CONTAINERCP_CORE_SERVICE_REGISTRY_H

#include "config/Config.h"
#include "core/ResourceManager.h"
#include "logger/Logger.h"
#include "site/SiteManager.h"
#include "storage/Storage.h"

namespace containercp::core {

class ServiceRegistry {
public:
    config::Config& config();
    logger::Logger& logger();
    ResourceManager& nodes();
    site::SiteManager& sites();
    void save();

private:
    friend class Application;
    ServiceRegistry();

    config::Config& config_;
    logger::Logger& logger_;
    ResourceManager nodes_;
    site::SiteManager sites_;
    storage::Storage storage_;
};

} // namespace containercp::core

#endif // CONTAINERCP_CORE_SERVICE_REGISTRY_H
