#ifndef CONTAINERCP_DOMAIN_DOMAIN_VIEW_SERVICE_H
#define CONTAINERCP_DOMAIN_DOMAIN_VIEW_SERVICE_H

#include "domain/DomainManager.h"
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
    DomainViewService(DomainManager& domains,
                      site::SiteManager& sites,
                      ssl::CertificateStore& cert_store);

    // Build enriched JSON array for all domains.
    std::string build_enriched_json() const;

    // Build enriched JSON for a single domain by ID.
    std::string build_enriched_json(uint64_t domain_id) const;

private:
    void write_enriched(std::ostringstream& json, const Domain& d) const;

    DomainManager& domains_;
    site::SiteManager& sites_;
    ssl::CertificateStore& cert_store_;
};

} // namespace containercp::domain

#endif // CONTAINERCP_DOMAIN_DOMAIN_VIEW_SERVICE_H
