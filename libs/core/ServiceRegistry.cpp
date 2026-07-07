#include "ServiceRegistry.h"

namespace containercp::core {

ServiceRegistry::ServiceRegistry()
    : config_(config::Config::instance())
    , logger_(logger::Logger::instance())
{
}

config::Config& ServiceRegistry::config() {
    return config_;
}

logger::Logger& ServiceRegistry::logger() {
    return logger_;
}

} // namespace containercp::core
