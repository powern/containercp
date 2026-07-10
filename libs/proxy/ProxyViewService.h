#ifndef CONTAINERCP_PROXY_PROXY_VIEW_SERVICE_H
#define CONTAINERCP_PROXY_PROXY_VIEW_SERVICE_H

#include "logger/Logger.h"
#include "proxy/ReverseProxyManager.h"
#include "proxy/NginxProxyProvider.h"
#include "site/SiteManager.h"
#include "ssl/CertificateStore.h"

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace containercp::proxy {

// Read-only view service that builds enriched proxy data for API/UI.
//
// ReverseProxyManager owns proxy records.
// SiteManager owns site information.
// CertificateStore owns SSL status.
// ProxyViewService only combines read-only data for presentation.
class ProxyViewService {
public:
    ProxyViewService(logger::Logger& logger,
                     ReverseProxyManager& proxies,
                     site::SiteManager& sites,
                     ssl::CertificateStore& cert_store,
                     NginxProxyProvider& proxy_provider);

    // Build enriched JSON array for all proxy entries.
    std::string build_enriched_json() const;

    // Build enriched JSON for a single proxy by domain.
    std::string build_enriched_json(const std::string& domain) const;

    // Build health JSON for the global proxy health endpoint.
    // Recovery state is passed from RecoveryManager (avoids circular dependency).
    std::string build_health_json(bool recovery_running, bool recovery_in_progress,
                                  std::time_t last_recovery_at,
                                  const std::string& last_recovery_result) const;

private:
    void write_enriched(std::ostringstream& json, const ReverseProxy& p) const;

    logger::Logger& logger_;
    ReverseProxyManager& proxies_;
    site::SiteManager& sites_;
    ssl::CertificateStore& cert_store_;
    NginxProxyProvider& proxy_provider_;
};

} // namespace containercp::proxy

#endif // CONTAINERCP_PROXY_PROXY_VIEW_SERVICE_H
