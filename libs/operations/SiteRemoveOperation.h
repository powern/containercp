#ifndef CONTAINERCP_OPERATIONS_SITE_REMOVE_OPERATION_H
#define CONTAINERCP_OPERATIONS_SITE_REMOVE_OPERATION_H

#include "backup/BackupManager.h"
#include "config/Config.h"
#include "core/OperationResult.h"
#include "database/DatabaseManager.h"
#include "domain/DomainManager.h"
#include "filesystem/Filesystem.h"
#include "mail/MailDomainManager.h"
#include "proxy/ProxyProvider.h"
#include "proxy/ReverseProxyManager.h"
#include "runtime/PortManager.h"
#include "runtime/Runtime.h"
#include "site/SiteManager.h"
#include "ssl/SslCertificateManager.h"

#include <cstdint>
#include <string>

namespace containercp::operations {

class SiteRemoveOperation {
public:
    SiteRemoveOperation(site::SiteManager& sites, domain::DomainManager& domains,
                        database::DatabaseManager& databases, backup::BackupManager& backups,
                        ssl::SslCertificateManager& ssl, mail::MailDomainManager& mail,
                        proxy::ReverseProxyManager& proxies,
                        proxy::ProxyProvider& proxy_provider,
                        runtime::PortManager& port_manager,
                        filesystem::Filesystem& fs, config::Config& cfg, runtime::Runtime& rt);

    core::OperationResult execute(const std::string& domain);

private:
    site::SiteManager& sites_;
    domain::DomainManager& domains_;
    database::DatabaseManager& databases_;
    backup::BackupManager& backups_;
    ssl::SslCertificateManager& ssl_;
    mail::MailDomainManager& mail_;
    proxy::ReverseProxyManager& proxies_;
    proxy::ProxyProvider& proxy_provider_;
    runtime::PortManager& port_manager_;
    filesystem::Filesystem& fs_;
    config::Config& cfg_;
    runtime::Runtime& rt_;
};

} // namespace containercp::operations

#endif // CONTAINERCP_OPERATIONS_SITE_REMOVE_OPERATION_H
