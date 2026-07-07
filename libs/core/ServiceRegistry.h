#ifndef CONTAINERCP_CORE_SERVICE_REGISTRY_H
#define CONTAINERCP_CORE_SERVICE_REGISTRY_H

#include "access/AccessGrantManager.h"
#include "access/AccessUserManager.h"
#include "access/LocalSftpProvider.h"
#include "backup/BackupManager.h"
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
#include "ssl/LetsEncryptProvider.h"
#include "ssl/SslCertificateManager.h"
#include "provider/DockerComposeProvider.h"
#include "runtime/DockerRuntime.h"
#include "site/SiteManager.h"
#include "storage/Storage.h"
#include "user/UserManager.h"

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
    database::DatabaseManager& databases();
    backup::BackupManager& backups();
    access::AccessUserManager& access_users();
    access::AccessGrantManager& access_grants();
    access::AccessProvider& access_provider();
    proxy::ReverseProxyManager& reverse_proxies();
    proxy::ProxyProvider& proxy_provider();
    ssl::SslCertificateManager& ssl();
    ssl::CertificateProvider& cert_provider();
    mail::MailDomainManager& mail();
    filesystem::Filesystem& filesystem();
    runtime::Runtime& runtime();
    provider::HostingProvider& hosting_provider();
    void save();

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
    database::DatabaseManager databases_;
    backup::BackupManager backups_;
    access::AccessUserManager access_users_;
    access::AccessGrantManager access_grants_;
    access::LocalSftpProvider access_provider_;
    proxy::ReverseProxyManager reverse_proxies_;
    proxy::NginxProxyProvider proxy_provider_;
    ssl::SslCertificateManager ssl_;
    ssl::LetsEncryptProvider cert_provider_;
    mail::MailDomainManager mail_;
    storage::Storage storage_;
    filesystem::Filesystem filesystem_;
    runtime::DockerRuntime runtime_;
    provider::DockerComposeProvider hosting_provider_;
};

} // namespace containercp::core

#endif // CONTAINERCP_CORE_SERVICE_REGISTRY_H
