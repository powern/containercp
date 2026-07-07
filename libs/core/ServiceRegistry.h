#ifndef CONTAINERCP_CORE_SERVICE_REGISTRY_H
#define CONTAINERCP_CORE_SERVICE_REGISTRY_H

#include "config/Config.h"
#include "logger/Logger.h"

namespace containercp::core {

class ServiceRegistry {
public:
    config::Config& config();
    logger::Logger& logger();

private:
    friend class Application;
    ServiceRegistry();

    config::Config& config_;
    logger::Logger& logger_;
};

} // namespace containercp::core

#endif // CONTAINERCP_CORE_SERVICE_REGISTRY_H
