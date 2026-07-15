#include "dns/DnsCheckService.h"
#include "doctest/doctest.h"

#include <cstring>
#include <string>
#include <vector>

using namespace containercp::dns;

TEST_CASE("DnsCheckService domain format validation") {
    // Direct format validation (no DNS lookup)
    CHECK(DnsCheckService::validate_domain("example.com"));
    CHECK(DnsCheckService::validate_domain("sub.domain.co.uk"));
    CHECK(DnsCheckService::validate_domain("a-b.com"));

    // Invalid: shell chars
    CHECK_FALSE(DnsCheckService::validate_domain("example.com; rm -rf /"));

    // Invalid: empty
    CHECK_FALSE(DnsCheckService::validate_domain(""));

    // Invalid: IP address
    CHECK_FALSE(DnsCheckService::validate_domain("192.168.1.1"));

    // Invalid: spaces
    CHECK_FALSE(DnsCheckService::validate_domain("exa mple.com"));

    // Invalid: leading dot
    CHECK_FALSE(DnsCheckService::validate_domain(".example.com"));

    // Invalid: trailing dot
    CHECK_FALSE(DnsCheckService::validate_domain("example.com."));

    // Invalid: too long
    std::string long_domain(300, 'a');
    CHECK_FALSE(DnsCheckService::validate_domain(long_domain));
}

TEST_CASE("DnsCheckService type validation") {
    // Direct type validation (no DNS lookup)
    CHECK(DnsCheckService::validate_type("A"));
    CHECK(DnsCheckService::validate_type("AAAA"));
    CHECK(DnsCheckService::validate_type("MX"));
    CHECK(DnsCheckService::validate_type("TXT"));
    CHECK(DnsCheckService::validate_type("CNAME"));
    CHECK(DnsCheckService::validate_type("NS"));
    CHECK(DnsCheckService::validate_type("SOA"));
    CHECK(DnsCheckService::validate_type("CAA"));

    // Invalid types
    CHECK_FALSE(DnsCheckService::validate_type("SPF"));
    CHECK_FALSE(DnsCheckService::validate_type("DMARC"));
    CHECK_FALSE(DnsCheckService::validate_type("INVALID"));
}

TEST_CASE("DnsCheckService caching") {
    DnsCheckService svc;

    // First call populates cache
    auto r1 = svc.check("example.com", {"A"});

    // Second call should return cached (same result object structure)
    auto r2 = svc.check("example.com", {"A"});
    CHECK(r2.success == r1.success);
    CHECK(r2.records.size() == r1.records.size());

    // Clear cache
    svc.clear_cache("example.com");

    // Cache evicted check: domain:type,type format
    svc.set_cache_ttl(0);  // instantly expire
    auto r3 = svc.check("example.com", {"A"});
    CHECK(r3.resolved_at >= r1.resolved_at);  // timestamp should be newer
}

TEST_CASE("DnsCheckService real DNS resolution") {
    DnsCheckService svc;

    // Test A record for a well-known domain
    auto r = svc.check("google.com", {"A"});
    CHECK(r.success);
    CHECK(r.status_code == "NOERROR");
    CHECK_FALSE(r.records.empty());

    // Should have at least one A record with valid IPv4
    bool found_a = false;
    for (const auto& rec : r.records) {
        if (rec.type == "A" && !rec.value.empty()) {
            found_a = true;
            CHECK(rec.ttl > 0);
            CHECK_FALSE(rec.dns_response_details.empty());
            break;
        }
    }
    CHECK(found_a);
}

TEST_CASE("DnsCheckService MX resolution") {
    DnsCheckService svc;

    // Test MX record
    auto r = svc.check("google.com", {"MX"});
    CHECK(r.success);
    CHECK_FALSE(r.records.empty());

    bool found_mx = false;
    for (const auto& rec : r.records) {
        if (rec.type == "MX") {
            found_mx = true;
            CHECK(rec.priority > 0);
            CHECK_FALSE(rec.value.empty());
            break;
        }
    }
    CHECK(found_mx);
}

TEST_CASE("DnsCheckService TXT resolution") {
    DnsCheckService svc;

    auto r = svc.check("google.com", {"TXT"});
    CHECK(r.success);
    CHECK_FALSE(r.records.empty());

    bool found_txt = false;
    for (const auto& rec : r.records) {
        if (rec.type == "TXT" && !rec.value.empty()) {
            found_txt = true;
            break;
        }
    }
    CHECK(found_txt);
}

TEST_CASE("DnsCheckService NXDOMAIN handling") {
    DnsCheckService svc;

    // Very unlikely to exist
    auto r = svc.check("thisshouldnotexistexample123456.com", {"A"});
    CHECK_FALSE(r.success);
    // Either NXDOMAIN, SERVFAIL, or TIMEOUT depending on network
    bool valid_status = (r.status_code == "NXDOMAIN" || r.status_code == "SERVFAIL" || r.status_code == "TIMEOUT");
    CHECK(valid_status);
}

TEST_CASE("DnsCheckService empty type list") {
    DnsCheckService svc;

    // No types requested = no records
    auto r = svc.check("example.com", {});
    CHECK(r.success);
    CHECK(r.records.empty());
    CHECK(r.status_code == "NOERROR");
}

TEST_CASE("DnsCheckService SOA resolution") {
    DnsCheckService svc;

    auto r = svc.check("google.com", {"SOA"});
    CHECK(r.success);
    // SOA may or may not be returned for some domains
}
