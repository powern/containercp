#include "ServiceRegistry.h"
#include "auth/AuthService.h"
#include "template/web_templates.h"

#include <filesystem>

namespace containercp::core {

ServiceRegistry::ServiceRegistry()
    : config_(config::Config::instance())
    , logger_(logger::Logger::instance())
    , backup_provider_(logger_)
    , access_provider_(logger_)
    , proxy_provider_(filesystem_, config_, logger_, ssl_)
    , cert_store_(logger_, config_.data_root() + "/ssl")
    , http01_challenge_(logger_, config_.data_root() + "/sites")
    , cert_provider_(std::make_shared<ssl::LetsEncryptProvider>(logger_, http01_challenge_, cert_store_))
    , pem_cert_provider_(std::make_shared<ssl::PemCertificateProvider>(logger_))
    , storage_(config_.database_dir())
    , job_executor_(jobs_, 2, 64)
    , renewal_scheduler_(logger_, cert_store_, jobs_, job_executor_, cert_providers_)
    , auth_(*this)
    , runtime_(logger_, config_.sites_dir())
    , hosting_provider_(filesystem_, config_, php_versions_, runtime_, profiles_)
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

        // Load or seed web server profiles (independent of PHP version state)
    {
        auto loaded_profiles = storage_.load_profiles();
        if (loaded_profiles.empty()) {
            auto migrated = storage_.migrate_template_profiles();
            if (!migrated.empty()) {
                profiles_.set_profiles(migrated);
                std::filesystem::remove(config_.database_dir() + "template_profiles.db");
            } else {
                auto tmpl = template_engine::default_web_templates();
                for (auto& [name, content] : tmpl) {
                    bool is_default = (name == "apache-php-default");
                    std::string path = config_.web_templates_dir() + name + ".conf.template";
                    filesystem_.create_directory(config_.web_templates_dir());
                    // Always overwrite template files to stay in sync with binary.
                    // This ensures template fixes in web_templates.h take effect
                    // even when the disk files already exist from a previous version.
                    filesystem_.create_file(path, content);
                    profiles_.create(name, profile::ProfileType::WEB_SERVER,
                                     name.find("apache") != std::string::npos ? "apache" : "nginx",
                                     path, name, is_default);
                }
            }
        } else {
            profiles_.set_profiles(loaded_profiles);
            // Even when profiles are loaded from disk, refresh the template files
            // on disk to ensure they match the current binary version.
            auto tmpl = template_engine::default_web_templates();
            filesystem_.create_directory(config_.web_templates_dir());
            for (auto& [name, content] : tmpl) {
                std::string path = config_.web_templates_dir() + name + ".conf.template";
                filesystem_.create_file(path, content);
            }
        }

        // Enforce apache-php-default as the only default WEB_SERVER profile.
        // This ensures existing installs that upgraded from old profiles.db
        // (which had nginx-php-default as default) are corrected.
        {
            auto profiles = profiles_.list();
            for (auto& p : profiles) {
                bool should_be_default = (p.profile_name == "apache-php-default"
                                          && p.type == profile::ProfileType::WEB_SERVER);
                p.default_profile = should_be_default;
            }
            profiles_.set_profiles(profiles);
        }
        storage_.save_profiles(profiles_.list());
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

    // Scan existing site directories to reclaim allocated ports
    port_manager_.scan_existing_sites(config_.sites_dir());

    auto loaded_auth_users = storage_.load_auth_users();
    if (!loaded_auth_users.empty()) {
        auth_users_.set_users(loaded_auth_users);
    }

    // Register certificate providers by string key
    cert_providers_["letsencrypt"] = cert_provider_;
    cert_providers_["pem"] = pem_cert_provider_;

    auth_.initialize();
}

void ServiceRegistry::start() {
    // Configure ACME staging: staging is default; production requires explicit opt-out
    const char* staging_env = std::getenv("LETSENCRYPT_STAGING");
    cert_provider_->set_staging(staging_env == nullptr || std::string(staging_env) != "0");

    // Recover certificates stuck in ISSUING state after crash
    for (auto site_id : cert_store_.enumerate()) {
        auto load_result = cert_store_.load_metadata(site_id);
        if (load_result.success && load_result.metadata.status == "issuing") {
            auto meta = load_result.metadata;
            meta.status = "error";
            meta.last_error = "Daemon restarted during certificate issuance.";
            meta.updated_at = ssl::CertificateStore::timestamp_utc();
            cert_store_.save_metadata(site_id, meta);
            logger_.warning("SYSTEM", "Recovered stuck ISSUING certificate for site "
                           + std::to_string(site_id));
        }
    }

    job_executor_.start();
    renewal_scheduler_.start();
}

void ServiceRegistry::shutdown() {
    renewal_scheduler_.shutdown();
    job_executor_.shutdown();
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

profile::ProfileManager& ServiceRegistry::profiles() {
    return profiles_;
}

database::DatabaseManager& ServiceRegistry::databases() {
    return databases_;
}

backup::BackupManager& ServiceRegistry::backups() {
    return backups_;
}

jobs::JobManager& ServiceRegistry::jobs() {
    return jobs_;
}

jobs::JobExecutor& ServiceRegistry::job_executor() {
    return job_executor_;
}

backup::BackupProvider& ServiceRegistry::backup_provider() {
    return backup_provider_;
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

ssl::CertificateStore& ServiceRegistry::cert_store() {
    return cert_store_;
}

ssl::CertificateProvider& ServiceRegistry::cert_provider() {
    return *cert_provider_;
}

ssl::CertificateProvider& ServiceRegistry::cert_provider_by_name(const std::string& name) {
    auto it = cert_providers_.find(name);
    if (it != cert_providers_.end()) {
        return *it->second;
    }
    return *cert_provider_;
}

std::unordered_map<std::string, std::shared_ptr<ssl::CertificateProvider>> ServiceRegistry::certificate_providers() {
    return cert_providers_;
}

ssl::RenewalScheduler& ServiceRegistry::renewal_scheduler() {
    return renewal_scheduler_;
}

mail::MailDomainManager& ServiceRegistry::mail() {
    return mail_;
}

auth::AuthUserManager& ServiceRegistry::auth_users() {
    return auth_users_;
}

auth::AuthService& ServiceRegistry::auth() {
    return auth_;
}

storage::Storage& ServiceRegistry::storage() {
    return storage_;
}

filesystem::Filesystem& ServiceRegistry::filesystem() {
    return filesystem_;
}

runtime::Runtime& ServiceRegistry::runtime() {
    return runtime_;
}

runtime::PortManager& ServiceRegistry::port_manager() {
    return port_manager_;
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
    storage_.save_profiles(profiles_.list());
    storage_.save_auth_users(auth_users_.list());
}

void ServiceRegistry::reload_profiles() {
    auto loaded = storage_.load_profiles();
    if (!loaded.empty()) {
        profiles_.set_profiles(loaded);
    }
}

} // namespace containercp::core
