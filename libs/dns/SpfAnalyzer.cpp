#include "SpfAnalyzer.h"
#include "DnsCheckService.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cstring>
#include <sstream>
#include <vector>

namespace containercp::dns {

SpfAnalyzer::SpfAnalyzer() {}

bool SpfAnalyzer::check_mechanism_ip4(const std::string& mech_value,
                                       const std::string& ip) {
    SpfAnalyzer a;
    return a.check_ip4_mechanism(mech_value, ip);
}

bool SpfAnalyzer::check_mechanism_ip6(const std::string& mech_value,
                                       const std::string& ip) {
    SpfAnalyzer a;
    return a.check_ip6_mechanism(mech_value, ip);
}

bool SpfAnalyzer::check_ip4_mechanism(const std::string& cidr,
                                       const std::string& ip) {
    struct in_addr addr, net;
    if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) return false;

    // Parse CIDR
    auto slash = cidr.find('/');
    std::string network = cidr.substr(0, slash);
    int prefix_len = 32;
    if (slash != std::string::npos) {
        try { prefix_len = std::stoi(cidr.substr(slash + 1)); } catch(...) { return false; }
        if (prefix_len < 0 || prefix_len > 32) return false;
    }

    if (inet_pton(AF_INET, network.c_str(), &net) != 1) return false;

    // Apply mask
    uint32_t addr_int = ntohl(addr.s_addr);
    uint32_t net_int = ntohl(net.s_addr);
    uint32_t mask_int = (prefix_len == 0) ? 0 : ~((1u << (32 - prefix_len)) - 1);

    return (addr_int & mask_int) == (net_int & mask_int);
}

bool SpfAnalyzer::check_ip6_mechanism(const std::string& cidr,
                                       const std::string& ip) {
    struct in6_addr addr, net;
    if (inet_pton(AF_INET6, ip.c_str(), &addr) != 1) return false;

    auto slash = cidr.find('/');
    std::string network = cidr.substr(0, slash);
    int prefix_len = 128;
    if (slash != std::string::npos) {
        try { prefix_len = std::stoi(cidr.substr(slash + 1)); } catch(...) { return false; }
        if (prefix_len < 0 || prefix_len > 128) return false;
    }

    if (inet_pton(AF_INET6, network.c_str(), &net) != 1) return false;

    // Compare bits up to prefix_len
    int full_bytes = prefix_len / 8;
    int remaining_bits = prefix_len % 8;
    if (memcmp(&addr, &net, full_bytes) != 0) return false;
    if (remaining_bits > 0) {
        unsigned char mask = 0xFF << (8 - remaining_bits);
        if ((addr.s6_addr[full_bytes] & mask) != (net.s6_addr[full_bytes] & mask))
            return false;
    }
    return true;
}

bool SpfAnalyzer::validate_syntax(const std::string& record,
                                   std::string& error_out) {
    if (record.empty()) {
        error_out = "Empty SPF record";
        return false;
    }
    // Must start with "v=spf1" (6 characters: v, =, s, p, f, 1)
    if (record.size() < 6 || record.substr(0, 6) != "v=spf1") {
        error_out = "SPF record must start with v=spf1";
        return false;
    }
    if (record.size() > 6 && record[6] != ' ' && record[6] != '\t') {
        error_out = "Invalid v=spf1 format";
        return false;
    }
    return true;
}

std::vector<SpfAnalyzer::SpfMechanism>
SpfAnalyzer::parse_mechanisms(const std::string& spf_record,
                              std::string& error_out) {
    std::vector<SpfMechanism> result;
    if (!validate_syntax(spf_record, error_out)) return result;

    // Skip "v=spf1" (6 chars) + whitespace
    size_t pos = 6;
    while (pos < spf_record.size() && (spf_record[pos] == ' ' || spf_record[pos] == '\t'))
        pos++;

    while (pos < spf_record.size()) {
        // Skip whitespace
        while (pos < spf_record.size() && (spf_record[pos] == ' ' || spf_record[pos] == '\t'))
            pos++;
        if (pos >= spf_record.size()) break;

        SpfMechanism mech;
        mech.qualifier = "+";  // default pass

        // Check qualifier
        if (spf_record[pos] == '+') { mech.qualifier = "+"; pos++; }
        else if (spf_record[pos] == '-') { mech.qualifier = "-"; pos++; }
        else if (spf_record[pos] == '~') { mech.qualifier = "~"; pos++; }
        else if (spf_record[pos] == '?') { mech.qualifier = "?"; pos++; }

        // Read mechanism name
        size_t start = pos;
        while (pos < spf_record.size() && spf_record[pos] != ' ' && spf_record[pos] != '\t'
               && spf_record[pos] != ':' && spf_record[pos] != '=')
            pos++;
        mech.mechanism = spf_record.substr(start, pos - start);

        // Read value after ':' or '='
        if (pos < spf_record.size() && (spf_record[pos] == ':' || spf_record[pos] == '=')) {
            pos++;  // skip : or =
            size_t val_start = pos;
            while (pos < spf_record.size() && spf_record[pos] != ' ' && spf_record[pos] != '\t')
                pos++;
            mech.domain = spf_record.substr(val_start, pos - val_start);
        }

        result.push_back(mech);
    }

    return result;
}

bool SpfAnalyzer::evaluate_mechanisms(
    const std::vector<SpfMechanism>& mechs,
    const std::string& expected_ipv4,
    const std::string& expected_ipv6,
    const std::string& domain,
    SpfAnalysis& result,
    int depth,
    std::map<std::string, bool>& visited_includes) {

    if (depth > 10) {
        result.errors.push_back("SPF include depth exceeded (max 10)");
        return false;
    }

    for (const auto& mech : mechs) {
        // DNS lookup count (RFC 7208): only a, mx, include, exists, redirect
        // consume lookups. ip4, ip6, and all do NOT count.
        bool consumes_lookup = (mech.mechanism == "a" || mech.mechanism == "mx"
                                || mech.mechanism == "include" || mech.mechanism == "exists"
                                || mech.mechanism == "redirect");
        if (consumes_lookup) {
            result.lookup_count++;
            if (result.lookup_count > 10) {
                result.errors.push_back("SPF DNS lookup count exceeded 10");
                return false;
            }
        }

        bool matched = false;
        std::string matched_by;

        if (mech.mechanism == "ip4" && !expected_ipv4.empty()) {
            if (check_ip4_mechanism(mech.domain, expected_ipv4)) {
                matched = true;
                matched_by = "ip4:" + mech.domain;
            }
        } else if (mech.mechanism == "ip6" && !expected_ipv6.empty()) {
            if (check_ip6_mechanism(mech.domain, expected_ipv6)) {
                matched = true;
                matched_by = "ip6:" + mech.domain;
            }
        } else if (mech.mechanism == "a") {
            // Resolve domain A/AAAA
            std::string resolve_domain = mech.domain.empty() ? domain : mech.domain;
            DnsCheckService svc;
            auto a_result = svc.check(resolve_domain, {"A"});
            if (a_result.success) {
                for (const auto& pt : a_result.per_type) {
                    if (pt.type == "A") {
                        for (const auto& rec : pt.records) {
                            if (rec.value == expected_ipv4) {
                                matched = true;
                                matched_by = "a:" + resolve_domain + " (" + rec.value + ")";
                                break;
                            }
                        }
                    }
                }
            }
        } else if (mech.mechanism == "mx") {
            std::string mx_domain = mech.domain.empty() ? domain : mech.domain;
            DnsCheckService svc;
            auto mx_result = svc.check(mx_domain, {"MX"});
            if (mx_result.success) {
                for (const auto& pt : mx_result.per_type) {
                    if (pt.type == "MX") {
                        for (const auto& rec : pt.records) {
                            std::string mx_target = rec.value;
                            if (!mx_target.empty()) {
                                // Resolve MX target A record
                                auto target_a = svc.check(mx_target, {"A"});
                                if (target_a.success) {
                                    for (const auto& tp : target_a.per_type) {
                                        if (tp.type == "A") {
                                            for (const auto& trec : tp.records) {
                                                if (trec.value == expected_ipv4) {
                                                    matched = true;
                                                    matched_by = "mx:" + mx_target + " (" + trec.value + ")";
                                                    break;
                                                }
                                            }
                                        }
                                        if (matched) break;
                                    }
                                }
                            }
                            if (matched) break;
                        }
                    }
                    if (matched) break;
                }
            }
        } else if (mech.mechanism == "include") {
            if (!mech.domain.empty()) {
                if (visited_includes.find(mech.domain) != visited_includes.end()) {
                    result.errors.push_back("SPF include loop detected: " + mech.domain);
                    continue;
                }
                visited_includes[mech.domain] = true;

                // Fetch SPF record from included domain
                DnsCheckService svc;
                auto txt_result = svc.check(mech.domain, {"TXT"});
                if (txt_result.success) {
                    for (const auto& pt : txt_result.per_type) {
                        if (pt.type == "TXT") {
                            for (const auto& rec : pt.records) {
                                if (rec.value.size() >= 6 && rec.value.substr(0, 6) == "v=spf1") {
                                    auto inner_mechs = parse_mechanisms(rec.value, result.errors.emplace_back());
                                    if (evaluate_mechanisms(inner_mechs, expected_ipv4,
                                        expected_ipv6, mech.domain, result, depth + 1, visited_includes)) {
                                        matched = true;
                                        matched_by = "include:" + mech.domain;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else if (mech.mechanism == "redirect") {
            if (!mech.domain.empty()) {
                // Follow redirect — fetch SPF from redirect target
                DnsCheckService svc;
                auto txt_result = svc.check(mech.domain, {"TXT"});
                if (txt_result.success) {
                    for (const auto& pt : txt_result.per_type) {
                        if (pt.type == "TXT") {
                            for (const auto& rec : pt.records) {
                                if (rec.value.size() >= 6 && rec.value.substr(0, 6) == "v=spf1") {
                                    auto redirect_mechs = parse_mechanisms(rec.value, result.errors.emplace_back());
                                    if (evaluate_mechanisms(redirect_mechs, expected_ipv4,
                                        expected_ipv6, mech.domain, result, depth + 1, visited_includes)) {
                                        matched = true;
                                        matched_by = "redirect:" + mech.domain;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else if (mech.mechanism == "all") {
            // all is the last resort — don't mark as matched_by
            result.all_qualifier = mech.qualifier;
        }

        if (matched) {
            result.expected_ip_allowed = true;
            result.mechanism_matched = matched_by;
            SpfCheck check;
            check.code = "spf_expected_ip";
            check.status = "ok";
            check.reason = "Expected IP is allowed by " + matched_by;
            result.checks.push_back(check);
        }

        // Always check for all qualifier (even after match) for warnings
        if (mech.mechanism == "all") {
            result.all_qualifier = mech.qualifier;
        }
    }

    return result.expected_ip_allowed;
}

SpfAnalysis SpfAnalyzer::analyze(const std::string& spf_record,
                                   const std::string& expected_ipv4,
                                   const std::string& expected_ipv6,
                                   const std::string& domain) {
    SpfAnalysis result;
    result.record = spf_record;

    if (spf_record.empty()) {
        result.status = "not_found";
        result.match = "not_published";
        return result;
    }

    // Syntax check
    std::string syntax_error;
    if (!validate_syntax(spf_record, syntax_error)) {
        result.status = "error";
        result.match = "error";
        result.errors.push_back(syntax_error);
        SpfCheck check;
        check.code = "spf_syntax";
        check.status = "error";
        check.reason = syntax_error;
        result.checks.push_back(check);
        return result;
    }

    SpfCheck syntax_check;
    syntax_check.code = "spf_syntax";
    syntax_check.status = "ok";
    syntax_check.reason = "SPF syntax is valid";
    result.checks.push_back(syntax_check);

    // Parse mechanisms
    std::string parse_error;
    auto mechs = parse_mechanisms(spf_record, parse_error);
    if (mechs.empty() && !parse_error.empty()) {
        result.status = "error";
        result.match = "error";
        result.errors.push_back(parse_error);
        return result;
    }

    // Evaluate
    bool ip_available = !expected_ipv4.empty() || !expected_ipv6.empty();
    if (!ip_available) {
        // Can't evaluate without expected IP
        result.status = "ok";
        result.match = "match";  // SPF exists, but can't verify IP — assume OK
        return result;
    }

    std::map<std::string, bool> visited;
    bool allowed = evaluate_mechanisms(mechs, expected_ipv4, expected_ipv6,
                                        domain, result, 0, visited);

    if (allowed) {
        result.expected_ip_allowed = true;
        result.status = "ok";
        result.match = "match";
    } else {
        result.expected_ip_allowed = false;
        result.status = "ok";
        result.match = "mismatch";
    }

    // Warnings for soft qualifiers
    if (result.expected_ip_allowed && result.all_qualifier == "~") {
        result.warnings.push_back("SPF softfail (~all): emails from unauthorized servers are marked but not rejected");
    } else if (result.expected_ip_allowed && result.all_qualifier == "?") {
        result.warnings.push_back("SPF neutral (?all): no clear policy");
    }

    return result;
}

} // namespace containercp::dns
