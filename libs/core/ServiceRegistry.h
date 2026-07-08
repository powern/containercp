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
#include "proxy/NginxProxyProvider.h"
#include "proxy/ReverseProxyManager.h"
#include "config/Config.h"
#include "core/ResourceManager.h"
#include "database/DatabaseManager.h"
#include "domain/DomainManager.h"
#include "filesystem/Filesystem.h"
#include "logger/Logger.h"
#include "mail/MailDomainManager.h"
#include "php/PhpVersionManager.h"
#include "runtime/PortManager.h"
#include "ssl/CertificateProvider.h"
#include "ssl/LetsEncryptProvider.h"
#include "ssl/CustomCertificateProvider.h"
#include "ssl/HTTP01ChallengeProvider.h"
#include "ssl/SslCertificateManager.h"
#include "profile/ProfileManager.h"
#include "provider/DockerComposeProvider.h"
#include "runtime/DockerRuntime.h"
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
    php::PhpVersionManager& php_versions();
    profile::ProfileManager& profiles();
    database::DatabaseManager& databases();
    backup::BackupManager& backups();
    backup::BackupProvider& backup_provider();
    jobs::JobManager& jobs();
    access::AccessUserManager& access_users();
    access::AccessGrantManager& access_grants();
    access::AccessProvider& access_provider();
    proxy::ReverseProxyManager& reverse_proxies();
    proxy::ProxyProvider& proxy_provider();
    ssl::SslCertificateManager& ssl();
    ssl::CertificateProvider& cert_provider();
    ssl::CertificateProvider& cert_provider_by_name(const std::string& name);
    std::unordered_map<std::string, ssl::CertificateProvider*> certificate_providers();
    mail::MailDomainManager& mail();
    filesystem::Filesystem& filesystem();
    runtime::Runtime& runtime();
    runtime::PortManager& port_manager();
    provider::HostingProvider& hosting_provider();
    auth::AuthUserManager& auth_users();
    auth::AuthService& auth();
    storage::Storage& storage();
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
    ssl::HTTP01ChallengeProvider http01_challenge_;
    ssl::LetsEncryptProvider cert_provider_;
    ssl::CustomCertificateProvider custom_cert_provider_;
    std::unordered_map<std::string, ssl::CertificateProvider*> cert_providers_;
    mail::MailDomainManager mail_;
    storage::Storage storage_;
    auth::AuthUserManager auth_users_;
    auth::AuthService auth_;
    filesystem::Filesystem filesystem_;
    runtime::DockerRuntime runtime_;
    runtime::PortManager port_manager_;
    provider::DockerComposeProvider hosting_provider_;
};

} // namespace containercp::core

#endif // CONTAINERCP_CORE_SERVICE_REGISTRY_H
