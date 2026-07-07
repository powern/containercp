#include "ServiceRegistry.h"

namespace containercp::core {

ServiceRegistry::ServiceRegistry()
    : config_(config::Config::instance())
    , logger_(logger::Logger::instance())
    , storage_(config_.database_dir())
    , runtime_(logger_, config_.sites_dir())
    , hosting_provider_(filesystem_, config_, php_versions_, runtime_)
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

    auto loaded_php = storage_.load_php_versions();
    if (loaded_php.empty()) {
        php_versions_.create("8.2", "php:8.2-fpm", false);
        php_versions_.create("8.3", "php:8.3-fpm", false);
        php_versions_.create("8.4", "php:8.4-fpm", true);
        storage_.save_php_versions(php_versions_.list());
    } else {
        php_versions_.set_versions(loaded_php);
    }

    auto loaded_domains = storage_.load_domains();
    if (!loaded_domains.empty()) {
        domains_.set_domains(loaded_domains);
    }

    auto loaded_databases = storage_.load_databases();
    if (!loaded_databases.empty()) {
        databases_.set_databases(loaded_databases);
    }

    auto loaded_backups = storage_.load_backups();
    if (!loaded_backups.empty()) {
        backups_.set_backups(loaded_backups);
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

domain::DomainManager& ServiceRegistry::domains() {
    return domains_;
}

php::PhpVersionManager& ServiceRegistry::php_versions() {
    return php_versions_;
}

database::DatabaseManager& ServiceRegistry::databases() {
    return databases_;
}

backup::BackupManager& ServiceRegistry::backups() {
    return backups_;
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
    storage_.save_domains(domains_.list());
    storage_.save_php_versions(php_versions_.list());
    storage_.save_databases(databases_.list());
    storage_.save_backups(backups_.list());
}

} // namespace containercp::core
