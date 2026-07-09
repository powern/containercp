#include "ServiceRegistry.h"
#include "auth/AuthService.h"
#include "template/web_templates.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace containercp::core {

ServiceRegistry::ServiceRegistry()
    : config_(config::Config::instance())
    , logger_(logger::Logger::instance())
    , backup_provider_(logger_)
    , access_provider_(logger_)
    , proxy_provider_(filesystem_, config_, logger_, ssl_, reverse_proxies_)
    , cert_store_(logger_, config_.data_root() + "/ssl")
    , http01_challenge_(logger_, config_.data_root() + "/sites", config_.data_root() + "/ssl/0/.well-known/acme-challenge")
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
        // Normalize upstreams from legacy database entries
        for (auto& p : loaded_proxies) {
            std::string normalized = proxy::ProxyConfigBuilder::normalize_upstream(p.upstream);
            std::string canonical = "site-" + std::to_string(p.site_id) + "-web:80";
            if (normalized != canonical) {
                logger_.info("SYSTEM", "Fixed upstream for " + p.domain
                             + ": '" + p.upstream + "' -> '" + canonical + "'");
                p.upstream = canonical;
            } else if (normalized != p.upstream) {
                logger_.info("SYSTEM", "Normalized upstream for " + p.domain
                             + ": '" + p.upstream + "' -> '" + normalized + "'");
                p.upstream = normalized;
            }
        }
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
    // Configure ACME environment: production is default; staging requires explicit opt-in
    const char* staging_env = std::getenv("LETSENCRYPT_STAGING");
    cert_provider_->set_staging(staging_env != nullptr && std::string(staging_env) == "1");

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

    // Setup admin panel proxy if server_hostname is configured
    {
        std::string hostname = config_.server_hostname();
        if (!hostname.empty()) {
            // Tell HTTP01ChallengeProvider about the admin hostname
            http01_challenge_.set_admin_hostname(hostname);
            // Auto-detect Docker gateway for admin upstream (host → container communication)
            std::string admin_upstream = "host.docker.internal:8081";
            {
                std::string gw_file = "/tmp/containercp-admin-gw.txt";
                std::string gw_cmd = "docker network inspect bridge --format '{{(index .IPAM.Config 0).Gateway}}' 2>/dev/null > " + gw_file;
                std::system(gw_cmd.c_str());
                std::ifstream gw_in(gw_file);
                std::string gw_ip;
                std::getline(gw_in, gw_ip);
                std::remove(gw_file.c_str());
                if (!gw_ip.empty()) {
                    admin_upstream = gw_ip + ":8081";
                    logger_.info("SYSTEM", "Docker gateway: " + gw_ip);
                }
            }

            // Check if this hostname already has a proxy entry
            auto* existing = reverse_proxies_.find_by_domain(hostname);
            if (!existing) {
                proxy::ReverseProxy admin_rp;
                admin_rp.domain = hostname;
                admin_rp.site_id = 0; // special: admin panel
                admin_rp.provider = "nginx";
                admin_rp.upstream = admin_upstream;
                admin_rp.enabled = true;
                admin_rp.status = "active";
                // Create admin proxy config with ACME challenge location inside server block
                proxy_provider_.create_proxy(admin_rp);
                {
                    proxy::ProxyConfigBuilder cfg_builder;
                    proxy::ProxyConfigBuilder::Params cfg_p;
                    cfg_p.domain = hostname;
                    cfg_p.upstream = admin_upstream;
                    cfg_p.acme_challenge_root = config_.data_root() + "/ssl/0/.well-known/acme-challenge";
                    std::string cfg = cfg_builder.build(cfg_p);
                    std::string cfg_path = config_.data_root() + "/proxy/sites/" + hostname + ".conf";
                    // Overwrite with config that includes ACME challenge location INSIDE server block
                    std::ofstream cfg_out(cfg_path);
                    if (cfg_out.is_open()) { cfg_out << cfg; }
                }
                {
                    reverse_proxies_.create(hostname, 0, config_.data_root() + "/proxy/sites/" + hostname + ".conf", admin_upstream);
                    logger_.info("SYSTEM", "Admin proxy created for " + hostname + " upstream=" + admin_upstream);

                    // Reload proxy so the new config takes effect
                    proxy_provider_.reload();

                    // Verify admin route is reachable
                    {
                        std::string verify_file = "/tmp/containercp-admin-verify.txt";
                        std::string verify_cmd = "wget -qO- --timeout=3 http://" + admin_upstream + "/ 2>/dev/null | head -c 100";
                        std::system((verify_cmd + " > " + verify_file + " 2>/dev/null").c_str());
                        std::ifstream vf(verify_file);
                        std::string result;
                        std::getline(vf, result);
                        std::remove(verify_file.c_str());
                        if (!result.empty()) {
                            logger_.info("SYSTEM", "Admin panel reachable via " + admin_upstream);
                        } else {
                            logger_.warning("SYSTEM", "Admin panel NOT reachable via " + admin_upstream
                                           + ". Web UI may not be accessible through proxy.");
                        }
                    }

                    // Check if SSL certificate exists for this domain
                    auto load_result = cert_store_.load_metadata(0); // site_id=0 for admin
                    if (load_result.success && load_result.metadata.status == "active") {
                        std::string cert_path = cert_store_.fullchain_path(0);
                        std::string key_path = cert_store_.privkey_path(0);
                        auto ssl_result = proxy_provider_.attach_certificate(hostname, cert_path, key_path);
                        if (ssl_result.success) {
                            logger_.info("SYSTEM", "Admin HTTPS enabled for " + hostname);
                        }
                    }
                }
            } else {
                logger_.info("SYSTEM", "Admin proxy already exists for " + hostname);
            }
        }
    }

    // Sync all HTTPS proxy configs on startup
    // Regenerates config from canonical upstream for every active HTTPS site.
    {
        for (auto site_id : cert_store_.enumerate()) {
            auto load_result = cert_store_.load_metadata(site_id);
            if (!load_result.success) continue;
            auto& meta = load_result.metadata;
            if (meta.status != "active" || !meta.https_enabled) continue;

            std::string domain = meta.domains.empty() ? "" : meta.domains[0];
            if (domain.empty()) continue;

            // Check if ACME environment matches the certificate issuer
            bool looks_like_staging = meta.issuer.find("STAGING") != std::string::npos
                                   || meta.issuer.find("Fake") != std::string::npos;
            if (meta.environment != "staging" && looks_like_staging) {
                logger_.warning("SYSTEM", domain + ": ACME=production but issuer is STAGING ("
                               + meta.issuer + "). Reissue certificate.");
            }
            logger_.info("SYSTEM", domain + ": env=" + meta.environment
                         + " issuer=\"" + meta.issuer + "\""
                         + " decision=" + (looks_like_staging ? "staging cert, reissue for production" : "ok"));

            // Log the upstream value from ReverseProxyManager for debugging
            auto* rp = reverse_proxies_.find_by_domain(domain);
            logger_.info("SYSTEM", "Pre-sync upstream for " + domain
                         + ": " + (rp ? (rp->upstream.empty() ? "EMPTY" : rp->upstream) : "NOT_FOUND"));

            std::string cert_path = cert_store_.fullchain_path(site_id);
            std::string key_path = cert_store_.privkey_path(site_id);

            logger_.info("SYSTEM", "Syncing HTTPS proxy config for " + domain
                         + ": cert=" + cert_path);
            auto result = proxy_provider_.attach_certificate(domain, cert_path, key_path);
            if (result.success) {
                logger_.info("SYSTEM", "Proxy config synced for " + domain);
            } else {
                logger_.warning("SYSTEM", "Proxy config sync failed for "
                               + domain + ": " + result.message);
            }
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
