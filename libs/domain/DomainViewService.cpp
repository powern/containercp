#include "DomainViewService.h"
#include "api/JsonFormatter.h"

#include <sstream>

namespace containercp::domain {

DomainViewService::DomainViewService(logger::Logger& logger,
                                     DomainManager& domains,
                                     site::SiteManager& sites,
                                     ssl::CertificateStore& cert_store)
    : logger_(logger)
    , domains_(domains)
    , sites_(sites)
    , cert_store_(cert_store)
{
}

void DomainViewService::write_enriched(std::ostringstream& json,
                                        const Domain& d) const {
    // Site info from SiteManager
    std::string site_name;
    std::string site_domain;
    auto* site = sites_.find_by_id(d.site_id);
    if (site) {
        site_name = site->name;
        site_domain = site->domain;
    }

    // SSL status from CertificateStore (single source of truth)
    std::string ssl_status = "Disabled";
    auto ssl_meta = cert_store_.load_metadata(d.site_id);
    if (ssl_meta.success) {
        ssl_status = cert_store_.https_display_status(ssl_meta.metadata);
    }

    json << "{\"id\":" << d.id
         << ",\"domain\":\"" << api::JsonFormatter::escape(d.fqdn)
         << "\",\"type\":\"" << api::JsonFormatter::escape(d.type)
         << "\",\"site_id\":" << d.site_id
         << ",\"site_name\":\"" << api::JsonFormatter::escape(site_name)
         << "\",\"site_domain\":\"" << api::JsonFormatter::escape(site_domain)
         << "\",\"target\":\"" << api::JsonFormatter::escape(d.target)
         << "\",\"ssl_enabled\":" << (d.ssl_enabled ? "true" : "false")
         << ",\"ssl_status\":\"" << api::JsonFormatter::escape(ssl_status)
         << "\",\"enabled\":" << (d.enabled ? "true" : "false")
         << "}";
}

std::string DomainViewService::build_enriched_json() const {
    auto all_domains = domains_.list();
    logger_.info("DOMAIN_VIEW", "Building enriched JSON for "
                 + std::to_string(all_domains.size()) + " domains");

    std::ostringstream json;
    json << "[";
    bool first = true;
    for (const auto& d : all_domains) {
        if (!first) json << ",";
        first = false;
        write_enriched(json, d);
    }
    json << "]";
    return json.str();
}

std::string DomainViewService::build_enriched_json(uint64_t domain_id) const {
    auto* d = domains_.find(domain_id);
    if (!d) return "null";
    std::ostringstream json;
    write_enriched(json, *d);
    return json.str();
}

} // namespace containercp::domain
