#include "ServiceRegistry.h"

namespace containercp::core {

ServiceRegistry::ServiceRegistry()
    : config_(config::Config::instance())
    , logger_(logger::Logger::instance())
{
    Resource res;
    res.name = "local";
    uint64_t id = nodes_.add(res);
    auto* node = nodes_.find(id);
    if (node != nullptr) {
        node->type = "local";
    }
}

config::Config& ServiceRegistry::config() {
    return config_;
}

logger::Logger& ServiceRegistry::logger() {
    return logger_;
}

ResourceManager& ServiceRegistry::nodes() {
    return nodes_;
}

} // namespace containercp::core
