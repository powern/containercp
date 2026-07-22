#include "Config.h"

#include <cstdlib>
#include <fstream>
#include <string>

namespace containercp::config {

Config::Config()
    : source_root_("/opt/containercp")
    , config_root_("/etc/containercp")
    , data_root_("/srv/containercp")
    , log_root_("/var/log/containercp")
    , storage_backend_("legacy")
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
    return data_root_ + "/templates/";
}

std::string Config::users_dir() const {
    return data_root_ + "/users/";
}

std::string Config::proxy_dir() const {
    return data_root_ + "/proxy/";
}

std::string Config::web_templates_dir() const {
    return templates_dir() + "web/";
}

std::string Config::server_hostname() const {
    return server_hostname_;
}

void Config::set_server_hostname(const std::string& hostname) {
    server_hostname_ = hostname;
    save_server_hostname();
}

void Config::load_server_hostname() {
    // Check env var first (overrides stored value)
    const char* env = std::getenv("SERVER_HOSTNAME");
    if (env != nullptr && env[0] != '\0') {
        server_hostname_ = env;
        return;
    }
    // Try to read from stored file
    std::string path = data_root_ + "/server_hostname";
    std::ifstream f(path);
    if (f.is_open()) {
        std::getline(f, server_hostname_);
    }
}

void Config::save_server_hostname() const {
    std::string path = data_root_ + "/server_hostname";
    std::ofstream f(path);
    if (f.is_open()) {
        f << server_hostname_;
    }
}

std::string Config::storage_backend() const {
    return storage_backend_;
}

void Config::set_storage_backend(const std::string& backend) {
    storage_backend_ = backend;
}

void Config::load_storage_backend() {
    const char* env = std::getenv("CONTAINERCP_STORAGE_BACKEND");
    if (env != nullptr && env[0] != '\0') {
        storage_backend_ = env;
        return;
    }
    std::string path = data_root_ + "/database/storage_backend";
    std::ifstream f(path);
    if (f.is_open()) {
        std::getline(f, storage_backend_);
    }
    if (storage_backend_.empty()) storage_backend_ = "legacy";
}

} // namespace containercp::config
