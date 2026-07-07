#include "ServiceRegistry.h"

namespace containercp::core {

ServiceRegistry::ServiceRegistry()
    : config_(config::Config::instance())
    , logger_(logger::Logger::instance())
    , storage_(config_.database_dir())
    , runtime_(logger_, config_.sites_dir())
    , hosting_provider_(filesystem_, config_, runtime_)
{
    auto loaded_nodes = storage_.load_nodes();
    if (loaded_nodes.empty()) {
        Resource res;
        res.name = "local";
        uint64_t id = nodes_.add(res);
        auto* node = nodes_.find(id);
        if (node != nullptr) {
            node->type = "local";
        }
        storage_.save_nodes(nodes_.list());
    } else {
        nodes_.set_nodes(loaded_nodes);
    }

    auto loaded_users = storage_.load_users();
    if (loaded_users.empty()) {
        users_.create("admin", 1000, config_.users_dir() + "admin", "/usr/sbin/nologin");
        filesystem_.create_directory(config_.users_dir() + "admin/sites/");
        filesystem_.create_directory(config_.users_dir() + "admin/logs/");
        filesystem_.create_directory(config_.users_dir() + "admin/tmp/");
        filesystem_.create_directory(config_.users_dir() + "admin/backups/");
        storage_.save_users(users_.list());
    } else {
        users_.set_users(loaded_users);
    }

    auto loaded_sites = storage_.load_sites();
    if (!loaded_sites.empty()) {
        sites_.set_sites(loaded_sites);
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

site::SiteManager& ServiceRegistry::sites() {
    return sites_;
}

user::UserManager& ServiceRegistry::users() {
    return users_;
}

filesystem::Filesystem& ServiceRegistry::filesystem() {
    return filesystem_;
}

runtime::Runtime& ServiceRegistry::runtime() {
    return runtime_;
}

provider::HostingProvider& ServiceRegistry::hosting_provider() {
    return hosting_provider_;
}

void ServiceRegistry::save() {
    storage_.save_nodes(nodes_.list());
    storage_.save_sites(sites_.list());
    storage_.save_users(users_.list());
}

} // namespace containercp::core
