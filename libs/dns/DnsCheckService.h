#ifndef CONTAINERCP_DNS_DNS_CHECK_SERVICE_H
#define CONTAINERCP_DNS_DNS_CHECK_SERVICE_H

#include <cstdint>
#include <ctime>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "dns/SpfAnalyzer.h"

namespace containercp::dns {

struct DnsRecord {
    std::string type;
    std::string name;
    std::string value;
    int ttl = 0;
    int priority = 0;
    std::string dns_response_details;
};

struct PerTypeResult {
    std::string type;
    std::string status_code;     // "NOERROR", "NXDOMAIN", "NODATA", "SERVFAIL", "TIMEOUT", "ERROR"
    std::string error;
    std::vector<DnsRecord> records;
};

struct DnsCheckResult {
    std::string domain;
    std::string resolved_at;
    std::vector<PerTypeResult> per_type;
    struct {
        std::string mname;
        std::string rname;
        uint64_t serial = 0;
    } soa;
    bool cached = false;
    std::string expected_ipv4;
    std::string expected_ipv6;
    std::string expected_ipv4_source;
    std::string expected_ipv6_source;
    std::string expected_ip_detected_at;
    bool expected_ip_stale = false;
    // SPF analysis (populated by API handler using SpfAnalyzer + NetworkService)
    struct {
        std::string status;
        std::string match;
        bool expected_ip_allowed = false;
        std::string record;
        std::string all_qualifier;
        int lookup_count = 0;
        std::string mechanism_matched;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
        std::vector<SpfCheck> checks;
    } spf_analysis;
    std::string overall_status;  // "complete" — all types ok, "partial" — some failed, "failed" — all failed
    bool success = false;
    std::string error;
};

class DnsCheckService {
public:
    DnsCheckService();

    DnsCheckResult check(const std::string& domain,
                          const std::vector<std::string>& record_types);

    void set_cache_ttl(int seconds);
    void clear_cache(const std::string& domain);
    bool has_cached(const std::string& domain) const;

    // Public for testing (format/type validation/computation, no DNS lookup)
    static bool validate_domain(const std::string& domain);
    static bool validate_dns_name(const std::string& name);
    static bool validate_type(const std::string& type);
    static std::string compute_overall_status(const std::vector<PerTypeResult>& per_type,
                                                bool& success_out,
                                                std::string& error_out);
    static int compute_http_status(const std::vector<PerTypeResult>& per_type,
                                    bool overall_success);

private:
    struct CacheEntry {
        DnsCheckResult result;
        time_t timestamp;
    };

    DnsCheckResult do_check(const std::string& domain,
                             const std::vector<std::string>& record_types);

    int cache_ttl_ = 60;
    std::map<std::string, CacheEntry> cache_;
    mutable std::mutex cache_mutex_;
};

} // namespace containercp::dns

#endif // CONTAINERCP_DNS_DNS_CHECK_SERVICE_H
