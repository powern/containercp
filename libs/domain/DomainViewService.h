#ifndef CONTAINERCP_DOMAIN_DOMAIN_VIEW_SERVICE_H
#define CONTAINERCP_DOMAIN_DOMAIN_VIEW_SERVICE_H

#include "domain/DomainManager.h"
#include "logger/Logger.h"
#include "mail/MailDomainManager.h"
#include "proxy/ReverseProxyManager.h"
#include "ssl/CertificateStore.h"
#include "site/SiteManager.h"

#include <string>
#include <vector>

namespace containercp::domain {

// Read-only view service that builds enriched domain data for API/UI.
//
// DomainManager owns domain records.
// SiteManager owns site information.
// CertificateStore owns SSL status.
// DomainViewService only combines read-only data for presentation.
class DomainViewService {
public:
    DomainViewService(logger::Logger& logger,
                      DomainManager& domains,
                      site::SiteManager& sites,
                      ssl::CertificateStore& cert_store,
                      mail::MailDomainManager& mail_domains,
                      proxy::ReverseProxyManager& reverse_proxies,
                      const std::string& server_hostname = "");

    // Build enriched JSON array for all domains.
    std::string build_enriched_json() const;

    // Build enriched JSON for a single domain by ID.
    std::string build_enriched_json(uint64_t domain_id) const;

private:
    void write_enriched(std::ostringstream& json, const Domain& d) const;

    logger::Logger& logger_;
    DomainManager& domains_;
    site::SiteManager& sites_;
    ssl::CertificateStore& cert_store_;
    mail::MailDomainManager& mail_domains_;
    proxy::ReverseProxyManager& reverse_proxies_;
    std::string server_hostname_;
};

} // namespace containercp::domain

#endif // CONTAINERCP_DOMAIN_DOMAIN_VIEW_SERVICE_H
