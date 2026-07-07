#ifndef CONTAINERCP_OPERATIONS_SITE_CREATE_OPERATION_H
#define CONTAINERCP_OPERATIONS_SITE_CREATE_OPERATION_H

#include "core/OperationResult.h"
#include "database/DatabaseManager.h"
#include "domain/DomainManager.h"
#include "node/Node.h"
#include "proxy/ReverseProxyManager.h"
#include "provider/HostingProvider.h"
#include "site/SiteManager.h"

#include <string>

namespace containercp::operations {

class SiteCreateOperation {
public:
    SiteCreateOperation(site::SiteManager& sites, domain::DomainManager& domains,
                        database::DatabaseManager& databases,
                        proxy::ReverseProxyManager& proxies,
                        provider::HostingProvider& provider);

    core::OperationResult execute(const std::string& owner, const std::string& domain, const node::Node& node, bool dry_run = false);

private:
    site::SiteManager& sites_;
    domain::DomainManager& domains_;
    database::DatabaseManager& databases_;
    proxy::ReverseProxyManager& proxies_;
    provider::HostingProvider& provider_;
};

} // namespace containercp::operations

#endif // CONTAINERCP_OPERATIONS_SITE_CREATE_OPERATION_H
