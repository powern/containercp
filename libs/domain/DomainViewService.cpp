#include "DomainViewService.h"
#include "api/JsonFormatter.h"

#include <sstream>
#include <stdexcept>

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
    logger_.info("DOMAIN_VIEW", "Processing domain id=" + std::to_string(d.id)
                 + " fqdn=" + d.fqdn + " site_id=" + std::to_string(d.site_id));

    // Site info from SiteManager
    std::string site_name;
    std::string site_domain;
    auto* site = sites_.find_by_id(d.site_id);
    if (site) {
        site_name = site->name;
        site_domain = site->domain;
        logger_.info("DOMAIN_VIEW", "  SiteManager found site=" + site_name
                     + " domain=" + site_domain);
    } else {
        logger_.info("DOMAIN_VIEW", "  SiteManager found NO site for site_id="
                     + std::to_string(d.site_id));
    }

    // SSL status from CertificateStore (single source of truth)
    std::string ssl_status = "Disabled";
    logger_.info("DOMAIN_VIEW", "  Loading SSL metadata for site_id="
                 + std::to_string(d.site_id));
    auto ssl_meta = cert_store_.load_metadata(d.site_id);
    if (ssl_meta.success) {
        logger_.info("DOMAIN_VIEW", "  SSL metadata loaded: status="
                     + ssl_meta.metadata.status
                     + " https_enabled=" + (ssl_meta.metadata.https_enabled ? "true" : "false"));
        ssl_status = cert_store_.https_display_status(ssl_meta.metadata);
        logger_.info("DOMAIN_VIEW", "  HTTPS display status=" + ssl_status);
    } else {
        logger_.info("DOMAIN_VIEW", "  SSL metadata NOT found (error="
                     + cert_store_.load_error_string(ssl_meta.error)
                     + ") message=" + ssl_meta.message);
    }

    // Build JSON
    logger_.info("DOMAIN_VIEW", "  Generating JSON for domain id=" + std::to_string(d.id));
    json << "{\"id\":" << d.id
         << ",\"domain\":\"" << api::JsonFormatter::escape(d.fqdn)
         << "\",\"type\":\"" << api::JsonFormatter::escape(d.type)
         << "\",\"site_id\":" << d.site_id
         << ",\"site_name\":\"" << api::JsonFormatter::escape(site_name)
         << "\",\"site_domain\":\"" << api::JsonFormatter::escape(site_domain)
         << "\",\"target\":\"" << api::JsonFormatter::escape(d.target)
         << ",\"ssl_enabled\":" << (d.ssl_enabled ? "true" : "false")
         << ",\"ssl_status\":\"" << api::JsonFormatter::escape(ssl_status)
         << ",\"enabled\":" << (d.enabled ? "true" : "false")
         << "}";
    logger_.info("DOMAIN_VIEW", "  JSON generation OK for domain id=" + std::to_string(d.id));
}

std::string DomainViewService::build_enriched_json() const {
    logger_.info("DOMAIN_VIEW", "build_enriched_json: START");
    auto all_domains = domains_.list();
    logger_.info("DOMAIN_VIEW", "build_enriched_json: loaded "
                 + std::to_string(all_domains.size()) + " domains");

    std::ostringstream json;
    json << "[";
    bool first = true;
    for (const auto& d : all_domains) {
        if (!first) json << ",";
        first = false;
        logger_.info("DOMAIN_VIEW", "build_enriched_json: processing domain id="
                     + std::to_string(d.id));
        write_enriched(json, d);
        logger_.info("DOMAIN_VIEW", "build_enriched_json: domain id="
                     + std::to_string(d.id) + " OK");
    }
    json << "]";
    auto result = json.str();
    logger_.info("DOMAIN_VIEW", "build_enriched_json: DONE ("
                 + std::to_string(result.size()) + " bytes)");
    return result;
}

std::string DomainViewService::build_enriched_json(uint64_t domain_id) const {
    logger_.info("DOMAIN_VIEW", "build_enriched_json(single): id="
                 + std::to_string(domain_id));
    auto* d = domains_.find(domain_id);
    if (!d) {
        logger_.info("DOMAIN_VIEW", "build_enriched_json(single): domain not found");
        return "null";
    }
    std::ostringstream json;
    write_enriched(json, *d);
    auto result = json.str();
    logger_.info("DOMAIN_VIEW", "build_enriched_json(single): DONE");
    return result;
}

} // namespace containercp::domain
