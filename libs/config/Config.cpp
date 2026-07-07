#include "Config.h"

namespace containercp::config {

Config::Config()
    : source_root_("/opt/containercp")
    , config_root_("/etc/containercp")
    , data_root_("/srv/containercp")
    , log_root_("/var/log/containercp")
{
}

Config& Config::instance() {
    static Config cfg;
    return cfg;
}

const std::string& Config::source_root() const {
    return source_root_;
}

const std::string& Config::config_root() const {
    return config_root_;
}

const std::string& Config::data_root() const {
    return data_root_;
}

const std::string& Config::log_root() const {
    return log_root_;
}

} // namespace containercp::config
