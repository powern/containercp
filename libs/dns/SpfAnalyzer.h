#ifndef CONTAINERCP_DNS_SPF_ANALYZER_H
#define CONTAINERCP_DNS_SPF_ANALYZER_H

#include <string>
#include <vector>
#include <map>

namespace containercp::dns {

struct SpfCheck {
    std::string code;
    std::string status;  // "ok", "error", "warning"
    std::string reason;
};

struct SpfAnalysis {
    std::string status;         // "ok", "error", "not_found"
    std::string match;          // "match", "mismatch", "not_published"
    bool expected_ip_allowed = false;
    std::string record;         // raw SPF record text
    std::string all_qualifier;  // "-", "~", "?", "+"
    int lookup_count = 0;
    std::string mechanism_matched;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::vector<SpfCheck> checks;
};

class SpfAnalyzer {
public:
    SpfAnalyzer();

    // Analyze SPF record. expected_ipv4/ipv6 come from NetworkService.
    // An empty IP means "not available" — analysis will be limited.
    SpfAnalysis analyze(const std::string& spf_record,
                        const std::string& expected_ipv4,
                        const std::string& expected_ipv6,
                        const std::string& domain);

    // Check if an IP is allowed by a specific SPF mechanism string
    static bool check_mechanism_ip4(const std::string& mech_value,
                                     const std::string& ip);
    static bool check_mechanism_ip6(const std::string& mech_value,
                                     const std::string& ip);

private:
    struct SpfMechanism {
        std::string qualifier;  // "+", "-", "~", "?"
        std::string mechanism;  // "ip4", "ip6", "a", "mx", "include", "all"
        std::string domain;     // target domain for a/mx/include
    };

    std::vector<SpfMechanism> parse_mechanisms(const std::string& spf_record,
                                                std::string& error_out);

    bool evaluate_mechanisms(const std::vector<SpfMechanism>& mechs,
                              const std::string& expected_ipv4,
                              const std::string& expected_ipv6,
                              const std::string& domain,
                              SpfAnalysis& result,
                              int depth,
                              std::map<std::string, bool>& visited_includes);

    bool check_ip4_mechanism(const std::string& cidr,
                              const std::string& ip);
    bool check_ip6_mechanism(const std::string& cidr,
                              const std::string& ip);

    // Validate SPF syntax
    bool validate_syntax(const std::string& record, std::string& error_out);
};

} // namespace containercp::dns

#endif // CONTAINERCP_DNS_SPF_ANALYZER_H
