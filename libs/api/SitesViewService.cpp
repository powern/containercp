#include "SitesViewService.h"
#include "JsonFormatter.h"
#include "proxy/ReverseProxyManager.h"
#include "site/Site.h"
#include "ssl/CertificateStore.h"

#include <sstream>

namespace containercp::api {

std::string build_enriched_sites_json(
    const std::vector<site::Site>& all_sites,
    const std::string& server_hostname,
    proxy::ReverseProxyManager& reverse_proxies,
    ssl::CertificateStore& cert_store)
{
    std::ostringstream json;
    json << "[";
    bool first = true;

    // Admin-panel virtual site (site_id=0)
    if (!server_hostname.empty()) {
        std::string proxy_upstream;
        std::string proxy_status = "Not verified";
        auto* rp = reverse_proxies.find_by_domain(server_hostname);
        if (rp) {
            proxy_upstream = rp->upstream;
            proxy_status = rp->enabled ? "Active" : "Disabled";
        }

        // SSL status from site_id=0
        std::string ssl_status = "Disabled";
        std::string ssl_https = "Inactive";
        auto ssl_meta = cert_store.load_metadata(0);
        if (ssl_meta.success) {
            ssl_status = cert_store.https_display_status(ssl_meta.metadata);
            if (ssl_meta.metadata.https_enabled) ssl_https = "Active";
        }

        // Web status derived from proxy state
        std::string web_status = "Not verified";
        if (rp && rp->enabled && !rp->upstream.empty()) web_status = "Available";
        else if (rp && !rp->enabled) web_status = "Disabled";

        json << "{\"id\":0"
             << ",\"name\":\"ContainerCP Admin\""
             << ",\"domain\":\"" << JsonFormatter::escape(server_hostname) << "\""
             << ",\"type\":\"system\""
             << ",\"system_role\":\"admin-panel\""
             << ",\"proxy_upstream\":\"" << JsonFormatter::escape(proxy_upstream) << "\""
             << ",\"proxy_status\":\"" << JsonFormatter::escape(proxy_status) << "\""
             << ",\"ssl_status\":\"" << JsonFormatter::escape(ssl_status) << "\""
             << ",\"ssl_https\":\"" << JsonFormatter::escape(ssl_https) << "\""
             << ",\"web_server\":\"nginx\""
             << ",\"node_id\":0"
             << ",\"owner\":\"system\""
             << ",\"site_id\":0"
             << ",\"enabled\":true"
             << ",\"web_status\":\"" << JsonFormatter::escape(web_status) << "\""
             << ",\"php_status\":\"N/A\""
             << ",\"https_status\":\"" << JsonFormatter::escape(ssl_https) << "\""
             << ",\"can_delete\":false"
             << ",\"can_manage_runtime\":false"
             << ",\"can_manage_php\":false"
             << ",\"can_manage_php_mail\":false"
             << ",\"can_manage_ssl\":true"
             << ",\"can_manage_proxy\":true"
             << ",\"can_manage_databases\":false"
             << ",\"can_manage_backups\":false"
             << ",\"can_manage_domains\":true"
             << "}";
        first = false;
    }

    for (const auto& site : all_sites) {
        if (!first) json << ",";
        first = false;
        json << JsonFormatter::site(site);
    }
    json << "]";
    return json.str();
}

} // namespace containercp::api
