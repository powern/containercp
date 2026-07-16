#ifndef CONTAINERCP_API_SITES_VIEW_SERVICE_H
#define CONTAINERCP_API_SITES_VIEW_SERVICE_H

#include <string>
#include <vector>

namespace containercp {
namespace site { struct Site; }
namespace proxy { class ReverseProxyManager; }
namespace ssl { class CertificateStore; }
namespace api {

// Build enriched sites JSON array, including the virtual admin-panel system site.
// The admin panel is derived from Config::server_hostname + ReverseProxyManager +
// CertificateStore (site_id=0), not from SiteManager.
std::string build_enriched_sites_json(
    const std::vector<site::Site>& all_sites,
    const std::string& server_hostname,
    proxy::ReverseProxyManager& reverse_proxies,
    ssl::CertificateStore& cert_store);

} // namespace api
} // namespace containercp

#endif // CONTAINERCP_API_SITES_VIEW_SERVICE_H
