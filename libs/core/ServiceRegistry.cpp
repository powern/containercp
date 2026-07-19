#include "ServiceRegistry.h"
#include "auth/AuthService.h"
#include "template/web_templates.h"
#include "utils/PasswordGenerator.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>

namespace containercp::core {

storage::StorageOptions ServiceRegistry::storage_backend_options(const config::Config& cfg) {
    storage::StorageOptions opts;
    std::string backend = cfg.storage_backend();
    if (backend == "sqlite") {
        opts.core_backend = storage::CoreStorageBackend::SqlitePhase5;
    } else if (backend != "legacy") {
        throw std::runtime_error(
            "Invalid storage.backend value: '" + backend + "'. "
            "Expected 'legacy' or 'sqlite'.");
    }
    return opts;
}

ServiceRegistry::ServiceRegistry()
    : config_(config::Config::instance())
    , logger_(logger::Logger::instance())
    , wordpress_config_(sites_)
    , wordpress_database_credentials_(wordpress_config_, databases_)
    , backup_provider_(logger_)
    , access_provider_(logger_)
    , proxy_provider_(filesystem_, config_, logger_, ssl_, reverse_proxies_)
    , cert_store_(logger_, config_.data_root() + "/ssl")
    , domain_view_(logger_, domains_, sites_, cert_store_, mail_, reverse_proxies_, config_.server_hostname())
    , network_(config_, logger_)
    , http01_challenge_(logger_, config_.data_root() + "/sites", config_.data_root() + "/ssl/0/.well-known/acme-challenge")
    , cert_provider_(std::make_shared<ssl::LetsEncryptProvider>(logger_, http01_challenge_, cert_store_))
    , pem_cert_provider_(std::make_shared<ssl::PemCertificateProvider>(logger_))
    , dkim_(logger_)
    , storage_(config_.database_dir(), storage_backend_options(config_))
    , mariadb_command_runner_(credential_command_executor_)
    , mariadb_credential_provider_(mariadb_command_runner_)
    , wordpress_runtime_runner_(credential_command_executor_)
    , wordpress_runtime_verifier_(wordpress_runtime_runner_)
    , database_credential_rotation_adapter_(
          sites_,
          databases_,
          wordpress_config_,
          wordpress_database_credentials_,
          wordpress_config_updater_,
          mariadb_credential_provider_,
          wordpress_runtime_verifier_,
          logger_,
          []() { return utils::PasswordGenerator::generate(48); },
          [this]() {
              storage_.save_databases(databases_.list());
              return true;
          },
          [this](const site::Site& site_record) {
              const auto compose_dir = (std::filesystem::path(config_.sites_dir()) / site_record.domain).string();
              return runtime_action_executor_.restart_services(compose_dir, {"php"}).success;
          },
          [this](const site::Site& site_record) {
              const auto status = site_runtime_.get_status(site_record.id, site_record.domain);
              return status.php.status == "Running" && status.db.status == "Running";
          })
    , database_credential_rotation_(database_credential_rotation_adapter_)
    , job_executor_(jobs_, 2, 64)
    , database_credential_rotation_jobs_(sites_, databases_, jobs_, job_executor_, database_credential_rotation_)
    , renewal_scheduler_(logger_, cert_store_, jobs_, job_executor_, cert_providers_)
    , auth_(*this)
    , runtime_(logger_, config_.sites_dir())
    , runtime_action_executor_(logger_)
    , site_runtime_(logger_, config_.sites_dir(), runtime_action_executor_)
    , mail_orchestrator_(mail_credentials_, runtime_, filesystem_, config_)
    , proxy_view_(logger_, reverse_proxies_, sites_, cert_store_, proxy_provider_, site_runtime_)
    , hosting_provider_(filesystem_, config_, php_versions_, runtime_, profiles_)
    , recovery_manager_(logger_, proxy_provider_, *this)
    , mail_provider_(logger_, config_.data_root())
{
    // Initial public IP detection (runs synchronously at startup, max ~8s total:
    // IPv4 routing_table + external_dns = ~3s, IPv6 same = ~3s, hostname fallback = ~2s)
    // This populates the cache so DNS diagnostics can provide expected IP immediately.
    network_.detect_now();

    // Register mail runtime sync callback
    runtime_sync_.register_handler("mail", [this]() -> core::OperationResult {
        if (mail_.module_state() != mail::MailModuleState::Active)
            return {true, "Mail module not active"};
        auto r = mail_provider_.apply_config(mail_.list(), mailboxes_, mail_aliases_);
        if (!r.success) {
            logger_.error("MAIL", "Config apply failed at stage '" + r.failed_stage + "': " + r.message);
        }
        return {r.success, r.message};
    });

    // Register mail health check
    health_.register_check("mail", [this]() -> runtime::HealthReport {
        if (mail_.module_state() != mail::MailModuleState::Active) {
            runtime::HealthReport r;
            r.status = "ok";
            r.services.push_back({"module", "ok", "inactive"});
            r.details = "{\"module_state\":\"inactive\"}";
            return r;
        }
        auto report = mail_provider_.check_health();

        // Enrich with module-level operational data
        uint64_t domain_count = 0, mailbox_count = 0, alias_count = 0;
        for (const auto& d : mail_.list()) {
            if (d.enabled) domain_count++;
        }
        mailbox_count = static_cast<uint64_t>(mailboxes_.list().size());
        alias_count = static_cast<uint64_t>(mail_aliases_.list().size());

        // Check certificate status
        std::string cert_path = config_.data_root() + "/ssl/0/fullchain.pem";
        std::string cert_status = "unknown";
        {
            runtime::CommandExecutor exec;
            auto check = exec.run({"openssl", "x509", "-in", cert_path, "-noout", "-subject"});
            if (check.exit_code == 0) {
                // Check if self-signed (subject == issuer)
                auto issuer = exec.run({"openssl", "x509", "-in", cert_path, "-noout", "-issuer"});
                auto expiry = exec.run({"openssl", "x509", "-in", cert_path, "-noout", "-checkend", "0"});
                if (expiry.exit_code != 0) {
                    cert_status = "expired";
                } else {
                    // Compare DN (subject vs issuer) to detect self-signed.
                    // openssl output: "subject=CN=..." and "issuer=CN=..."
                    auto dn = [](const std::string& line) -> std::string {
                        auto eq = line.find('=');
                        return (eq != std::string::npos) ? line.substr(eq + 1) : line;
                    };
                    if (issuer.exit_code == 0 && dn(issuer.out) == dn(check.out)) {
                        cert_status = "self-signed";
                    } else {
                        cert_status = "valid";
                    }
                }
            } else {
                cert_status = "missing";
            }
        }

        report.details = "{\"module_state\":\"active\""
            ",\"domain_count\":" + std::to_string(domain_count)
            + ",\"mailbox_count\":" + std::to_string(mailbox_count)
            + ",\"alias_count\":" + std::to_string(alias_count)
            + ",\"certificate\":\"" + cert_status + "\"}";
        return report;
    });

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

    const std::string CONTAINERCP_PHP_IMAGE = "ghcr.io/powern/containercp-php:8.4";

    auto loaded_php = storage_.load_php_versions();
    if (loaded_php.empty()) {
        php_versions_.create("8.4", CONTAINERCP_PHP_IMAGE, true);
        storage_.save_php_versions(php_versions_.list());
    } else {
        // Idempotent migration: replace legacy official PHP images with ContainerCP images
        bool migrated = false;
        for (auto& pv : loaded_php) {
            if (pv.image == "php:8.4-fpm" || pv.image == "php:8.3-fpm" || pv.image == "php:8.2-fpm") {
                std::string old_img = pv.image;
                pv.image = CONTAINERCP_PHP_IMAGE;
                logger_.info("PHP", "Migrated PHP image " + old_img + " -> " + CONTAINERCP_PHP_IMAGE);
                migrated = true;
            }
        }
        if (migrated) {
            storage_.save_php_versions(loaded_php);
        }
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
    auto loaded_mailboxes = storage_.load_mailboxes();
    if (!loaded_mailboxes.empty()) {
        mailboxes_.set_mailboxes(loaded_mailboxes);
    }
    auto loaded_aliases = storage_.load_mail_aliases();
    if (!loaded_aliases.empty()) {
        mail_aliases_.set_aliases(loaded_aliases);
    }

    // Load mail module state (default: inactive)
    std::string mail_state = storage_.load_mail_module_state();
    if (!mail_state.empty()) {
        mail_.set_module_state(mail::mail_module_state_from_string(mail_state));
    }
    // Load smarthost config
    mail_.smarthost_from_string(storage_.load_mail_smarthost());
    // Propagate to mail provider for config generation
    const auto& sc = mail_.smarthost();
    if (sc.enabled && !sc.host.empty()) {
        mail_provider_.set_smarthost(sc.host, sc.port, sc.username, sc.password);
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
            // Skip site_id=0 (admin/system entries) — these use host gateway, not Docker containers
            if (p.site_id == 0) continue;

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

    // One-time migration: legacy 5-field sites get php_mail_enabled detected
    // from artifacts (msmtprc). Uses a global marker file in the sites directory.
    // After the first migration, subsequent restarts respect explicit values.
    {
        std::string migration_marker = config_.data_root() + "/.sites-mail-migrated";
        struct stat marker_stat;
        bool already_migrated = (stat(migration_marker.c_str(), &marker_stat) == 0);

        if (!already_migrated) {
            bool migrated = false;
            for (auto& site : loaded_sites) {
                // Only migrate legacy 5-field records (php_mail_enabled_present == false)
                if (site.php_mail_enabled_present) continue;
                if (site.php_mail_enabled) continue;
                std::string msmtprc_path = config_.sites_dir() + site.domain + "/config/php/msmtprc";
                if (filesystem_.exists(msmtprc_path)) {
                    site.php_mail_enabled = true;
                    migrated = true;
                    logger_.info("MAIL", "Migrated legacy site " + site.domain + ": php_mail_enabled=true");
                }
            }
            if (migrated) {
                storage_.save_sites(loaded_sites);
            }
            // Create marker — migration runs only once
            std::ofstream marker(migration_marker);
        }
    }

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

std::string ServiceRegistry::detect_docker_gateway(logger::Logger& log) {
    std::string gw_file = "/tmp/containercp-gateway.txt";
    std::string gw_cmd = "docker network inspect bridge --format '{{(index .IPAM.Config 0).Gateway}}' 2>/dev/null > " + gw_file;
    std::system(gw_cmd.c_str());
    std::ifstream gw_in(gw_file);
    std::string gw_ip;
    std::getline(gw_in, gw_ip);
    std::remove(gw_file.c_str());

    // Trim whitespace
    gw_ip.erase(0, gw_ip.find_first_not_of(" \t\n\r"));
    gw_ip.erase(gw_ip.find_last_not_of(" \t\n\r") + 1);

    if (!gw_ip.empty()) {
        log.info("SYSTEM", "Docker gateway: " + gw_ip);
        return gw_ip;
    }

    log.info("SYSTEM", "Docker gateway not detected, using host.docker.internal");
    return "host.docker.internal";
}

core::OperationResult ServiceRegistry::ensure_admin_proxy() {
    std::string hostname = config_.server_hostname();
    if (hostname.empty()) {
        return {false, "server_hostname not configured"};
    }

    {
        auto* nginx_proxy = dynamic_cast<proxy::NginxProxyProvider*>(&proxy_provider_);
        if (nginx_proxy) {
            nginx_proxy->set_webmail_upstream("containercp-mail-snappymail:80");
        }
    }
    http01_challenge_.set_admin_hostname(hostname);

    std::string gw_ip = detect_docker_gateway(logger_);
    std::string admin_upstream = gw_ip + ":8081";
    logger_.info("SYSTEM", "Admin upstream: " + admin_upstream);

    // Ensure ReverseProxy entry exists with correct upstream
    auto* existing = reverse_proxies_.find_by_domain(hostname);
    if (!existing) {
        proxy::ReverseProxy admin_rp;
        admin_rp.domain = hostname;
        admin_rp.site_id = 0;
        admin_rp.provider = "nginx";
        admin_rp.upstream = admin_upstream;
        admin_rp.enabled = true;
        admin_rp.status = "active";
        proxy_provider_.create_proxy(admin_rp);
        reverse_proxies_.create(hostname, 0, config_.data_root() + "/proxy/sites/" + hostname + ".conf", admin_upstream);
        logger_.info("SYSTEM", "Admin proxy entry created for " + hostname);
    } else if (existing->upstream != admin_upstream) {
        // Update upstream if it changed (e.g., site-0-web → admin UI)
        existing->upstream = admin_upstream;
        storage_.save_reverse_proxies(reverse_proxies_.list());
        logger_.info("SYSTEM", "Admin proxy upstream updated to " + admin_upstream);
    }

    // Always regenerate admin proxy config file (supports config updates)
    {
        proxy::ProxyConfigBuilder cfg_builder;
        proxy::ProxyConfigBuilder::Params cfg_p;
        cfg_p.domain = hostname;
        cfg_p.upstream = admin_upstream;
        cfg_p.acme_challenge_root = config_.data_root() + "/ssl/0/.well-known/acme-challenge";
        cfg_p.webmail_upstream = "containercp-mail-snappymail:80";
        std::string cfg = cfg_builder.build(cfg_p);
        std::string cfg_path = config_.data_root() + "/proxy/sites/" + hostname + ".conf";
        std::ofstream cfg_out(cfg_path);
        if (cfg_out.is_open()) { cfg_out << cfg; }
        logger_.info("SYSTEM", "Admin proxy config regenerated for " + hostname);
    }

    // Reload proxy so the new config takes effect
    auto reload_result = proxy_provider_.reload();
    if (!reload_result.success) {
        logger_.warning("SYSTEM", "Admin proxy reload failed: " + reload_result.message);
    }

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

    // Check if SSL certificate exists for admin domain (site_id=0)
    auto load_result = cert_store_.load_metadata(0);
    if (load_result.success && load_result.metadata.status == "active") {
        std::string cert_path = cert_store_.fullchain_path(0);
        std::string key_path = cert_store_.privkey_path(0);
        auto ssl_result = proxy_provider_.attach_certificate(hostname, cert_path, key_path);
        if (ssl_result.success) {
            logger_.info("SYSTEM", "Admin HTTPS enabled for " + hostname);
        }
    }

    return {true, "Admin proxy configured for " + hostname};
}

void ServiceRegistry::sync_all_https_configs() {
    // Delegate to declarative sync: synchronize all proxy configs with the database
    auto* nginx_proxy = dynamic_cast<proxy::NginxProxyProvider*>(&proxy_provider_);
    if (nginx_proxy) {
        auto result = nginx_proxy->sync_all_proxies(reverse_proxies_.list(), cert_store_);
        if (result.success) {
            logger_.info("SYSTEM", "Declarative proxy sync completed");
        } else {
            logger_.warning("SYSTEM", "Declarative proxy sync had issues: " + result.message);
        }
    } else {
        // Fallback: old per-cert sync for non-nginx providers (should not happen)
        for (auto site_id : cert_store_.enumerate()) {
            auto load_result = cert_store_.load_metadata(site_id);
            if (!load_result.success) continue;
            auto& meta = load_result.metadata;
            if (meta.status != "active" || !meta.https_enabled) continue;
            std::string domain = meta.domains.empty() ? "" : meta.domains[0];
            if (domain.empty()) continue;
            auto* rp = reverse_proxies_.find_by_domain(domain);
            if (!rp) {
                logger_.warning("SYSTEM", domain + ": proxy entry not found, skipping HTTPS sync");
                continue;
            }
            std::string cert_path = cert_store_.fullchain_path(site_id);
            std::string key_path = cert_store_.privkey_path(site_id);
            proxy_provider_.attach_certificate(domain, cert_path, key_path);
        }
    }
}

void ServiceRegistry::start() {
    // Recover certificates stuck in ISSUING state after crash
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

    // Admin proxy setup + HTTPS config sync
    ensure_admin_proxy();
    sync_all_https_configs();

    // Mail configuration recovery: regenerate config and ensure connectivity
    // for sites that have PHP Mail enabled.
    {
        auto sync_result = runtime_sync_.sync("mail");
        if (!sync_result.success) {
            logger_.warning("MAIL", "Mail config regeneration at startup: " + sync_result.message);
        }

        for (const auto& site : sites_.list()) {
            if (site.php_mail_enabled) {
                runtime_.connect_mail_network(site.id, site.domain);
            }
        }
    }

    job_executor_.start();
    renewal_scheduler_.start();

    // Start background proxy health monitor (after all startup init is complete)
    recovery_manager_.start();
}

void ServiceRegistry::shutdown() {
    recovery_manager_.shutdown();
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

domain::DomainViewService& ServiceRegistry::domain_view() {
    return domain_view_;
}

dns::DnsCheckService& ServiceRegistry::dns_check() {
    return dns_check_;
}

network::NetworkService& ServiceRegistry::network() {
    return network_;
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

database::DatabaseCredentialRotationJobService& ServiceRegistry::database_credential_rotation_jobs() {
    return database_credential_rotation_jobs_;
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

proxy::ProxyViewService& ServiceRegistry::proxy_view() {
    return proxy_view_;
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

mail::MailboxManager& ServiceRegistry::mailboxes() {
    return mailboxes_;
}

mail::DkimManager& ServiceRegistry::dkim() {
    return dkim_;
}

mail::SiteMailCredentials& ServiceRegistry::mail_credentials() {
    return mail_credentials_;
}

mail::SiteMailOrchestrator& ServiceRegistry::mail_orchestrator() {
    return mail_orchestrator_;
}

mail::MailAliasManager& ServiceRegistry::mail_aliases() {
    return mail_aliases_;
}

mail::MailProvider& ServiceRegistry::mail_provider() {
    return mail_provider_;
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

wordpress::WordPressConfigService& ServiceRegistry::wordpress_config() {
    return wordpress_config_;
}

wordpress::WordPressDatabaseCredentialResolver& ServiceRegistry::wordpress_database_credentials() {
    return wordpress_database_credentials_;
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

runtime::SiteRuntimeManager& ServiceRegistry::site_runtime() {
    return site_runtime_;
}

runtime::RuntimeActionExecutor& ServiceRegistry::runtime_executor() {
    return runtime_action_executor_;
}

runtime::RuntimeSynchronizer& ServiceRegistry::runtime_sync() {
    return runtime_sync_;
}

runtime::HealthRegistry& ServiceRegistry::health() {
    return health_;
}

provider::HostingProvider& ServiceRegistry::hosting_provider() {
    return hosting_provider_;
}

core::RecoveryManager& ServiceRegistry::recovery() {
    return recovery_manager_;
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
    storage_.save_mailboxes(mailboxes_.list());
    storage_.save_mail_aliases(mail_aliases_.list());
    storage_.save_mail_module_state(mail::mail_module_state_to_string(mail_.module_state()));
    storage_.save_mail_smarthost(mail_.smarthost_to_string());
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
