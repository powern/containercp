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

std::string Config::database_dir() const {
    return data_root_ + "/database/";
}

std::string Config::sites_dir() const {
    return data_root_ + "/sites/";
}

std::string Config::templates_dir() const {
    return config_root_ + "/templates/";
}

std::string Config::users_dir() const {
    return data_root_ + "/users/";
}

std::string Config::proxy_dir() const {
    return data_root_ + "/proxy/";
}

std::string Config::web_templates_dir() const {
    return config_root_ + "/templates/web/";
}

} // namespace containercp::config
