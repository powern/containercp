#ifndef CONTAINERCP_CORE_SERVICE_REGISTRY_H
#define CONTAINERCP_CORE_SERVICE_REGISTRY_H

#include "access/AccessGrantManager.h"
#include "access/AccessUserManager.h"
#include "auth/AuthService.h"
#include "auth/AuthUserManager.h"
#include "access/LocalSftpProvider.h"
#include "backup/BackupManager.h"
#include "backup/TarBackupProvider.h"
#include "jobs/JobManager.h"
#include "jobs/JobExecutor.h"
#include "proxy/NginxProxyProvider.h"
#include "proxy/ProxyViewService.h"
#include "proxy/ReverseProxyManager.h"
#include "config/Config.h"
#include "core/ResourceManager.h"
#include "core/OperationResult.h"
#include "core/RecoveryManager.h"
#include "database/DatabaseManager.h"
#include "dns/DnsCheckService.h"
#include "network/NetworkService.h"
#include "domain/DomainManager.h"
#include "filesystem/Filesystem.h"
#include "logger/Logger.h"
#include "mail/DkimManager.h"
#include "mail/MailDomainManager.h"
#include "mail/MailAliasManager.h"
#include "mail/MailboxManager.h"
#include "mail/SiteMailCredentials.h"
#include "mail/SiteMailOrchestrator.h"
#include "mail/providers/DockerMailProvider.h"
#include "php/PhpVersionManager.h"
#include "runtime/PortManager.h"
#include "runtime/RuntimeActionExecutor.h"
#include "runtime/RuntimeSynchronizer.h"
#include "runtime/HealthReport.h"
#include "runtime/SiteRuntimeManager.h"
#include "ssl/CertificateProvider.h"
#include "ssl/CertificateStore.h"
#include "ssl/LetsEncryptProvider.h"
#include "ssl/PemCertificateProvider.h"
#include "ssl/HTTP01ChallengeProvider.h"
#include "ssl/RenewalScheduler.h"
#include "ssl/SslCertificateManager.h"

#include <memory>
#include "profile/ProfileManager.h"
#include "provider/DockerComposeProvider.h"
#include "runtime/DockerRuntime.h"
#include "domain/DomainViewService.h"
#include "site/SiteManager.h"
#include "storage/Storage.h"
#include "user/UserManager.h"

#include <unordered_map>

namespace containercp::core {

class ServiceRegistry {
public:
    config::Config& config();
    logger::Logger& logger();
    ResourceManager& nodes();
    site::SiteManager& sites();
    user::UserManager& users();
    domain::DomainManager& domains();
    domain::DomainViewService& domain_view();
    dns::DnsCheckService& dns_check();
    network::NetworkService& network();
    php::PhpVersionManager& php_versions();
    profile::ProfileManager& profiles();
    database::DatabaseManager& databases();
    backup::BackupManager& backups();
    backup::BackupProvider& backup_provider();
    jobs::JobManager& jobs();
    jobs::JobExecutor& job_executor();
    access::AccessUserManager& access_users();
    access::AccessGrantManager& access_grants();
    access::AccessProvider& access_provider();
    proxy::ReverseProxyManager& reverse_proxies();
    proxy::ProxyProvider& proxy_provider();
    proxy::ProxyViewService& proxy_view();
    ssl::SslCertificateManager& ssl();
    ssl::CertificateStore& cert_store();
    ssl::CertificateProvider& cert_provider();
    ssl::CertificateProvider& cert_provider_by_name(const std::string& name);
    std::unordered_map<std::string, std::shared_ptr<ssl::CertificateProvider>> certificate_providers();
    ssl::RenewalScheduler& renewal_scheduler();
    mail::MailDomainManager& mail();
    mail::MailboxManager& mailboxes();
    mail::DkimManager& dkim();
    mail::SiteMailCredentials& mail_credentials();
    mail::SiteMailOrchestrator& mail_orchestrator();
    filesystem::Filesystem& filesystem();
    mail::MailAliasManager& mail_aliases();
    mail::MailProvider& mail_provider();
    runtime::Runtime& runtime();
    runtime::PortManager& port_manager();
    runtime::SiteRuntimeManager& site_runtime();
    runtime::RuntimeActionExecutor& runtime_executor();
    runtime::RuntimeSynchronizer& runtime_sync();
    runtime::HealthRegistry& health();
    provider::HostingProvider& hosting_provider();
    auth::AuthUserManager& auth_users();
    auth::AuthService& auth();
    storage::Storage& storage();
    core::RecoveryManager& recovery();
    // Detect Docker bridge gateway address for Web UI binding and proxy upstream.
    // Returns "host.docker.internal" if detection fails (works with --add-host flag).
    static std::string detect_docker_gateway(logger::Logger& log);

    // Create or update the admin panel reverse proxy entry and nginx config.
    // Idempotent — safe to call multiple times.  No-op if server_hostname is empty.
    core::OperationResult ensure_admin_proxy();

    // Regenerate HTTPS nginx configs for all sites with active certificates.
    // Safe to call at any time — validates config before reloading nginx.
    void sync_all_https_configs();

    void start();
    void shutdown();
    void save();
    void reload_profiles();

private:
    friend class Application;
    ServiceRegistry();

    config::Config& config_;
    logger::Logger& logger_;
    ResourceManager nodes_;
    site::SiteManager sites_;
    user::UserManager users_;
    domain::DomainManager domains_;
    php::PhpVersionManager php_versions_;
    profile::ProfileManager profiles_;
    database::DatabaseManager databases_;
    backup::BackupManager backups_;
    jobs::JobManager jobs_;
    backup::TarBackupProvider backup_provider_;
    access::AccessUserManager access_users_;
    access::AccessGrantManager access_grants_;
    access::LocalSftpProvider access_provider_;
    proxy::ReverseProxyManager reverse_proxies_;
    proxy::NginxProxyProvider proxy_provider_;
    ssl::SslCertificateManager ssl_;
    ssl::CertificateStore cert_store_;
    domain::DomainViewService domain_view_;
    dns::DnsCheckService dns_check_;
    network::NetworkService network_;
    ssl::HTTP01ChallengeProvider http01_challenge_;
    std::shared_ptr<ssl::LetsEncryptProvider> cert_provider_;
    std::shared_ptr<ssl::PemCertificateProvider> pem_cert_provider_;
    std::unordered_map<std::string, std::shared_ptr<ssl::CertificateProvider>> cert_providers_;
    mail::MailDomainManager mail_;
    mail::MailAliasManager mail_aliases_;
    mail::MailboxManager mailboxes_;
    mail::DkimManager dkim_;
    mail::SiteMailCredentials mail_credentials_;
    storage::Storage storage_;
    jobs::JobExecutor job_executor_;
    ssl::RenewalScheduler renewal_scheduler_;
    auth::AuthUserManager auth_users_;
    auth::AuthService auth_;
    filesystem::Filesystem filesystem_;
    runtime::DockerRuntime runtime_;
    runtime::PortManager port_manager_;
    runtime::RuntimeActionExecutor runtime_action_executor_;
    runtime::SiteRuntimeManager site_runtime_;
    runtime::RuntimeSynchronizer runtime_sync_;
    runtime::HealthRegistry health_;
    mail::SiteMailOrchestrator mail_orchestrator_;
    proxy::ProxyViewService proxy_view_;
    provider::DockerComposeProvider hosting_provider_;
    core::RecoveryManager recovery_manager_;
    mail::DockerMailProvider mail_provider_;
};

} // namespace containercp::core

#endif // CONTAINERCP_CORE_SERVICE_REGISTRY_H
