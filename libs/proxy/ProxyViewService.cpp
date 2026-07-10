#include "ProxyViewService.h"
#include "api/JsonFormatter.h"

#include <fstream>
#include <sstream>
#include <ctime>

namespace containercp::proxy {

ProxyViewService::ProxyViewService(logger::Logger& logger,
                                   ReverseProxyManager& proxies,
                                   site::SiteManager& sites,
                                   ssl::CertificateStore& cert_store,
                                   NginxProxyProvider& proxy_provider)
    : logger_(logger)
    , proxies_(proxies)
    , sites_(sites)
    , cert_store_(cert_store)
    , proxy_provider_(proxy_provider)
{
}

void ProxyViewService::write_enriched(std::ostringstream& json,
                                       const ReverseProxy& p) const {
    // Site info from SiteManager
    std::string site_name;
    auto* site = (p.site_id > 0) ? sites_.find_by_id(p.site_id) : nullptr;
    if (site) {
        site_name = site->name;
    } else if (p.site_id == 0) {
        site_name = "ContainerCP Admin";
    }

    // SSL status from CertificateStore
    bool https_enabled = false;
    bool redirect_enabled = false;
    auto ssl_meta = cert_store_.load_metadata(p.site_id);
    if (ssl_meta.success) {
        https_enabled = ssl_meta.metadata.https_enabled;
        redirect_enabled = ssl_meta.metadata.redirect_enabled;
    }

    bool is_protected = (p.site_id == 0);

    // Build allowed_actions list
    std::string allowed = is_protected
        ? "[\"open\",\"test\",\"sync\"]"
        : "[\"open\",\"remove\"]";

    json << "{"
         << "\"id\":" << p.id
         << ",\"domain\":\"" << api::JsonFormatter::escape(p.domain)
         << "\",\"entry_type\":\"" << (is_protected ? "system" : "site")
         << "\",\"site_id\":" << p.site_id
         << ",\"site_name\":\"" << api::JsonFormatter::escape(site_name)
         << "\",\"upstream\":\"" << api::JsonFormatter::escape(p.upstream)
         << "\",\"configured_state\":\"" << api::JsonFormatter::escape(p.status)
         << "\",\"backend_health\":\"unknown\""
         << ",\"http_enabled\":true"
         << ",\"https_enabled\":" << (https_enabled ? "true" : "false")
         << ",\"redirect_enabled\":" << (redirect_enabled ? "true" : "false")
         << ",\"protected\":" << (is_protected ? "true" : "false")
         << ",\"allowed_actions\":" << allowed
         << "}";
}

std::string ProxyViewService::build_enriched_json() const {
    std::ostringstream json;
    json << "[";
    bool first = true;
    for (const auto& p : proxies_.list()) {
        if (!first) json << ",";
        first = false;
        write_enriched(json, p);
    }
    json << "]";
    return json.str();
}

std::string ProxyViewService::build_enriched_json(const std::string& domain) const {
    auto* p = proxies_.find_by_domain(domain);
    if (!p) return "null";
    std::ostringstream json;
    write_enriched(json, *p);
    return json.str();
}

std::string ProxyViewService::build_health_json(
    bool recovery_running, bool recovery_in_progress,
    std::time_t last_recovery_at,
    const std::string& last_recovery_result) const {

    std::ostringstream json;

    bool running = proxy_provider_.central_proxy_running();
    auto config_test = proxy_provider_.last_test_result();

    // Detect nginx version via docker exec (one-shot, cached for daemon lifetime)
    static std::string cached_version;
    if (cached_version.empty() && running) {
        std::string vf = "/tmp/containercp-nginx-version.txt";
        std::system(("docker exec containercp-proxy nginx -v 2> " + vf).c_str());
        std::ifstream vin(vf);
        std::string vline;
        std::getline(vin, vline);
        std::remove(vf.c_str());
        auto vp = vline.find("nginx/");
        if (vp != std::string::npos) {
            vp += 6;
            while (vp < vline.size() && ((vline[vp] >= '0' && vline[vp] <= '9') || vline[vp] == '.')) {
                cached_version += vline[vp++];
            }
        }
        if (cached_version.empty()) cached_version = "?";
    }

    int total = 0, system_count = 0, site_count = 0;
    for (const auto& p : proxies_.list()) {
        total++;
        if (p.site_id == 0) system_count++;
        else site_count++;
    }

    // Format last_recovery_at
    std::string recovery_time_str;
    if (last_recovery_at > 0) {
        char buf[32];
        struct tm tm_buf;
        gmtime_r(&last_recovery_at, &tm_buf);
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
        recovery_time_str = "\"" + std::string(buf) + "\"";
    } else {
        recovery_time_str = "null";
    }

    json << "{"
         << "\"container\":{"
         << "\"name\":\"containercp-proxy\""
         << ",\"state\":\"" << (running ? "running" : "stopped")
         << "\",\"healthy\":" << (running ? "true" : "false")
         << "}"
         << ",\"proxy\":{"
         << "\"provider\":\"nginx\""
         << ",\"version\":\"" << api::JsonFormatter::escape(cached_version)
         << "\",\"config_test\":{"
         << "\"success\":" << (config_test.success ? "true" : "false")
         << ",\"message\":\"" << api::JsonFormatter::escape(config_test.message)
         << "\"}"
         << "}"
         << ",\"recovery\":{"
         << "\"manager_running\":" << (recovery_running ? "true" : "false")
         << ",\"recovery_in_progress\":" << (recovery_in_progress ? "true" : "false")
         << ",\"last_recovery_at\":" << recovery_time_str
         << ",\"last_recovery_result\":\"" << api::JsonFormatter::escape(last_recovery_result)
         << "\",\"scope\":\"since_daemon_start\""
         << "}"
         << ",\"entries\":{"
         << "\"total\":" << total
         << ",\"system\":" << system_count
         << ",\"site\":" << site_count
         << "}"
         << "}";

    return json.str();
}

} // namespace containercp::proxy
