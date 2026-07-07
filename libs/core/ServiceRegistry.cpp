#include "ServiceRegistry.h"
#include "template/web_templates.h"

namespace containercp::core {

ServiceRegistry::ServiceRegistry()
    : config_(config::Config::instance())
    , logger_(logger::Logger::instance())
    , access_provider_(logger_)
    , proxy_provider_(filesystem_, config_, logger_, ssl_)
    , cert_provider_(logger_)
    , storage_(config_.database_dir())
    , runtime_(logger_, config_.sites_dir())
    , hosting_provider_(filesystem_, config_, php_versions_, runtime_, template_profiles_)
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

    auto loaded_templates = storage_.load_template_profiles();
    if (loaded_templates.empty()) {
        auto tmpl = template_engine::default_web_templates();
        for (auto& [name, content] : tmpl) {
            bool is_default = (name == "nginx-php-default");
            std::string path = config_.web_templates_dir() + name + ".conf.template";
            filesystem_.create_directory(config_.web_templates_dir());
            if (!filesystem_.exists(path)) {
                filesystem_.create_file(path, content);
            }
            template_profiles_.create(name, name.find("apache") != std::string::npos ? "apache" : "nginx",
                                      path, name, is_default);
        }
        storage_.save_template_profiles(template_profiles_.list());
    } else {
        template_profiles_.set_profiles(loaded_templates);
    }
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

    auto loaded_ssl = storage_.load_ssl_certificates();
    if (!loaded_ssl.empty()) {
        ssl_.set_certificates(loaded_ssl);
    }

    auto loaded_mail = storage_.load_mail_domains();
    if (!loaded_mail.empty()) {
        mail_.set_domains(loaded_mail);
    }

    auto loaded_access = storage_.load_access_users();
    if (!loaded_access.empty()) {
        access_users_.set_users(loaded_access);
    }

    auto loaded_grants = storage_.load_access_grants();
    if (!loaded_grants.empty()) {
        access_grants_.set_grants(loaded_grants);
    }

    auto loaded_proxies = storage_.load_reverse_proxies();
    if (!loaded_proxies.empty()) {
        reverse_proxies_.set_proxies(loaded_proxies);
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

template_engine::TemplateProfileManager& ServiceRegistry::template_profiles() {
    return template_profiles_;
}

database::DatabaseManager& ServiceRegistry::databases() {
    return databases_;
}

backup::BackupManager& ServiceRegistry::backups() {
    return backups_;
}

access::AccessUserManager& ServiceRegistry::access_users() {
    return access_users_;
}

access::AccessGrantManager& ServiceRegistry::access_grants() {
    return access_grants_;
}

access::AccessProvider& ServiceRegistry::access_provider() {
    return access_provider_;
}

proxy::ReverseProxyManager& ServiceRegistry::reverse_proxies() {
    return reverse_proxies_;
}

proxy::ProxyProvider& ServiceRegistry::proxy_provider() {
    return proxy_provider_;
}

ssl::SslCertificateManager& ServiceRegistry::ssl() {
    return ssl_;
}

ssl::CertificateProvider& ServiceRegistry::cert_provider() {
    return cert_provider_;
}

mail::MailDomainManager& ServiceRegistry::mail() {
    return mail_;
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
    storage_.save_ssl_certificates(ssl_.list());
    storage_.save_mail_domains(mail_.list());
    storage_.save_access_users(access_users_.list());
    storage_.save_access_grants(access_grants_.list());
    storage_.save_reverse_proxies(reverse_proxies_.list());
    storage_.save_template_profiles(template_profiles_.list());
}

void ServiceRegistry::reload_template_profiles() {
    auto loaded = storage_.load_template_profiles();
    if (!loaded.empty()) {
        template_profiles_.set_profiles(loaded);
    }
}

} // namespace containercp::core
