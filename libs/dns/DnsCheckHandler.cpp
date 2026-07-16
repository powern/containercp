#include "DnsCheckHandler.h"
#include "DnsCheckService.h"
#include "SpfAnalyzer.h"
#include "api/JsonFormatter.h"
#include "network/NetworkService.h"

#include <sstream>
#include <string>
#include <vector>

namespace containercp {
namespace dns {

api::Response handleDnsCheck(
    const api::Request& req,
    DnsCheckService& svc,
    containercp::network::NetworkService* net)
{
    using api::JsonFormatter;
    api::Response r;

    std::string remaining = req.path.substr(std::string("/api/domains/").size());
    const std::string suffix = "/dns-check";

    if (remaining.size() <= suffix.size()
        || remaining.substr(remaining.size() - suffix.size()) != suffix) {
        r.status_code = 404;
        r.body = "{\"success\":false,\"error\":\"Not found\"}";
        return r;
    }

    std::string domain_raw = remaining.substr(0, remaining.size() - suffix.size());
    if (domain_raw.empty()) {
        r.status_code = 400;
        r.body = "{\"success\":false,\"error\":\"Invalid domain\"}";
        return r;
    }

    std::string domain;
    domain.reserve(domain_raw.size());
    for (char c : domain_raw) {
        domain.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (!DnsCheckService::validate_dns_name(domain)) {
        r.status_code = 400;
        r.body = "{\"success\":false,\"error\":\"Invalid domain format\"}";
        return r;
    }

    std::vector<std::string> types;
    bool refresh = false;

    auto it = req.query.find("refresh");
    if (it != req.query.end() && it->second == "1") refresh = true;

    it = req.query.find("types");
    if (it != req.query.end() && !it->second.empty()) {
        const std::string& v = it->second;
        size_t pos = 0;
        while (pos < v.size()) {
            auto comma = v.find(',', pos);
            std::string t = v.substr(pos, comma - pos);
            if (!t.empty()) types.push_back(t);
            pos = (comma == std::string::npos) ? v.size() : comma + 1;
        }
    }

    if (types.empty()) {
        types = {"A", "AAAA", "MX", "TXT", "NS", "SOA", "CAA"};
    }

    for (const auto& t : types) {
        if (!DnsCheckService::validate_type(t)) {
            r.status_code = 400;
            r.body = "{\"success\":false,\"error\":\"Unsupported DNS record type: "
                + JsonFormatter::escape(t) + "\"}";
            return r;
        }
    }

    if (refresh) {
        svc.clear_cache(domain);
    }

    auto result = svc.check(domain, types);

    if (net) {
        auto v4 = net->public_ipv4();
        auto v6 = net->public_ipv6();
        result.expected_ipv4 = v4.address;
        result.expected_ipv6 = v6.address;
        result.expected_ipv4_source = v4.source;
        result.expected_ipv6_source = v6.source;
        result.expected_ip_detected_at = v4.detected_at.empty() ? v6.detected_at : v4.detected_at;
        result.expected_ip_stale = v4.stale || v6.stale;

        SpfAnalyzer spf;
        std::string spf_record;
        int spf_count = 0;
        for (const auto& pt : result.per_type) {
            if (pt.type == "TXT") {
                for (const auto& rec : pt.records) {
                    if (rec.value.size() >= 6 && rec.value.substr(0, 6) == "v=spf1") {
                        spf_record = rec.value;
                        spf_count++;
                        if (spf_count > 1) break;
                    }
                }
            }
            if (spf_count > 1) break;
        }
        if (spf_count > 1) {
            result.spf_analysis.status = "error";
            result.spf_analysis.match = "error";
            result.spf_analysis.record = spf_record;
            result.spf_analysis.errors.push_back("Multiple SPF records found (" + std::to_string(spf_count) + ")");
            SpfCheck spf_check;
            spf_check.code = "spf_syntax";
            spf_check.status = "error";
            spf_check.reason = "Duplicate SPF records: " + std::to_string(spf_count) + " TXT records start with v=spf1";
            result.spf_analysis.checks.push_back(spf_check);
        } else {
            result.spf_analysis = spf.analyze(spf_record, result.expected_ipv4,
                                                result.expected_ipv6, domain);
        }
    }

    std::ostringstream json;
    json << "{\"success\":"
         << (result.success ? "true" : "false")
         << ",\"data\":{"
         << "\"domain\":\"" << JsonFormatter::escape(result.domain)
         << "\",\"resolved_at\":\"" << JsonFormatter::escape(result.resolved_at)
         << "\",\"cached\":" << (result.cached ? "true" : "false")
         << ",\"overall_status\":\"" << JsonFormatter::escape(result.overall_status)
         << "\",\"expected_ipv4\":\"" << JsonFormatter::escape(result.expected_ipv4)
         << "\",\"expected_ipv6\":\"" << JsonFormatter::escape(result.expected_ipv6)
         << "\",\"expected_ipv4_source\":\"" << JsonFormatter::escape(result.expected_ipv4_source)
         << "\",\"expected_ipv6_source\":\"" << JsonFormatter::escape(result.expected_ipv6_source)
         << "\",\"expected_ip_detected_at\":\"" << JsonFormatter::escape(result.expected_ip_detected_at)
         << "\",\"expected_ip_stale\":" << (result.expected_ip_stale ? "true" : "false")
         << ",\"spf_analysis\":{"
         << "\"status\":\"" << JsonFormatter::escape(result.spf_analysis.status)
         << "\",\"match\":\"" << JsonFormatter::escape(result.spf_analysis.match)
         << "\",\"expected_ip_allowed\":" << (result.spf_analysis.expected_ip_allowed ? "true" : "false")
         << ",\"record\":\"" << JsonFormatter::escape(result.spf_analysis.record)
         << "\",\"all_qualifier\":\"" << JsonFormatter::escape(result.spf_analysis.all_qualifier)
         << "\",\"lookup_count\":" << result.spf_analysis.lookup_count
         << ",\"mechanism_matched\":\"" << JsonFormatter::escape(result.spf_analysis.mechanism_matched)
         << "\",\"errors\":[";
    for (size_t i = 0; i < result.spf_analysis.errors.size(); ++i) {
        if (i > 0) json << ",";
        json << "\"" << JsonFormatter::escape(result.spf_analysis.errors[i]) << "\"";
    }
    json << "],\"warnings\":[";
    for (size_t i = 0; i < result.spf_analysis.warnings.size(); ++i) {
        if (i > 0) json << ",";
        json << "\"" << JsonFormatter::escape(result.spf_analysis.warnings[i]) << "\"";
    }
    json << "],\"checks\":[";
    for (size_t i = 0; i < result.spf_analysis.checks.size(); ++i) {
        if (i > 0) json << ",";
        json << "{\"code\":\"" << JsonFormatter::escape(result.spf_analysis.checks[i].code)
             << "\",\"status\":\"" << JsonFormatter::escape(result.spf_analysis.checks[i].status)
             << "\",\"reason\":\"" << JsonFormatter::escape(result.spf_analysis.checks[i].reason) << "\"}";
    }
    json << "]}"
         << ",\"per_type\":[";

    bool first_pt = true;
    for (const auto& pt : result.per_type) {
        if (!first_pt) json << ",";
        first_pt = false;
        json << "{\"type\":\"" << JsonFormatter::escape(pt.type)
             << "\",\"status_code\":\"" << JsonFormatter::escape(pt.status_code)
             << "\",\"error\":\"" << JsonFormatter::escape(pt.error)
             << "\",\"records\":[";
        bool first_rec = true;
        for (const auto& rec : pt.records) {
            if (!first_rec) json << ",";
            first_rec = false;
            json << "{\"type\":\"" << JsonFormatter::escape(rec.type)
                 << "\",\"name\":\"" << JsonFormatter::escape(rec.name)
                 << "\",\"value\":\"" << JsonFormatter::escape(rec.value)
                 << "\",\"ttl\":" << rec.ttl
                 << ",\"priority\":" << rec.priority
                 << ",\"dns_response_details\":\"" << JsonFormatter::escape(rec.dns_response_details)
                 << "\"}";
        }
        json << "]}";
    }

    json << "],\"soa\":{"
         << "\"mname\":\"" << JsonFormatter::escape(result.soa.mname)
         << "\",\"rname\":\"" << JsonFormatter::escape(result.soa.rname)
         << "\",\"serial\":" << result.soa.serial
         << "}";

    if (!result.error.empty()) {
        json << ",\"error\":\"" << JsonFormatter::escape(result.error) << "\"";
    }

    json << "}}";
    r.body = json.str();
    r.status_code = DnsCheckService::compute_http_status(result.per_type, result.success);

    return r;
}

} // namespace containercp::dns
} // namespace containercp
