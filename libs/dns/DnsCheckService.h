#ifndef CONTAINERCP_DNS_DNS_CHECK_SERVICE_H
#define CONTAINERCP_DNS_DNS_CHECK_SERVICE_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace containercp::dns {

struct DnsRecord {
    std::string type;
    std::string name;
    std::string value;
    int ttl = 0;
    int priority = 0;
    std::string dns_response_details;
};

struct DnsCheckResult {
    std::string domain;
    std::string resolved_at;
    std::vector<DnsRecord> records;
    struct {
        std::string mname;
        std::string rname;
        uint64_t serial = 0;
    } soa;
    std::string status_code;
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

    // Public for testing (format/type validation, no DNS lookup)
    static bool validate_domain(const std::string& domain);
    static bool validate_type(const std::string& type);

private:
    struct CacheEntry {
        DnsCheckResult result;
        time_t timestamp;
    };

    DnsCheckResult do_check(const std::string& domain,
                             const std::vector<std::string>& record_types);

    int cache_ttl_ = 60;
    std::map<std::string, CacheEntry> cache_;
};

} // namespace containercp::dns

#endif // CONTAINERCP_DNS_DNS_CHECK_SERVICE_H
