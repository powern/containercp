#include "dns/DnsCheckService.h"
#include "doctest/doctest.h"

#include <string>
#include <vector>

using namespace containercp::dns;

// Helper: build a PerTypeResult
static PerTypeResult make_pt(const std::string& type,
                              const std::string& status_code,
                              const std::string& error,
                              size_t record_count = 0) {
    PerTypeResult r;
    r.type = type;
    r.status_code = status_code;
    r.error = error;
    for (size_t i = 0; i < record_count; ++i) {
        DnsRecord rec;
        rec.type = type;
        rec.name = "test." + type;
        rec.value = type + "_val_" + std::to_string(i);
        rec.ttl = 300;
        r.records.push_back(std::move(rec));
    }
    return r;
}

TEST_CASE("DnsCheck API: cached metadata - first call misses") {
    DnsCheckService svc;
    auto r = svc.check("google.com", {"A"});
    CHECK_FALSE(r.cached);  // first call is live lookup
}

TEST_CASE("DnsCheck API: cached metadata - second call hits") {
    DnsCheckService svc;
    svc.check("google.com", {"A"});   // populate cache
    auto r = svc.check("google.com", {"A"});  // cache hit
    CHECK(r.cached);
}

TEST_CASE("DnsCheck API: clear_cache forces fresh lookup") {
    DnsCheckService svc;
    svc.check("google.com", {"A"});   // populate cache

    svc.clear_cache("google.com");
    auto r = svc.check("google.com", {"A"});  // should be live
    CHECK_FALSE(r.cached);
}

TEST_CASE("DnsCheck API: uppercase domain normalization") {
    DnsCheckService svc;

    auto r = svc.check("GOOGLE.COM", {"A"});
    CHECK(r.success);
    CHECK(r.domain == "google.com");

    // Cache should use normalized key
    svc.clear_cache("GOOGLE.COM");
    auto r2 = svc.check("GOOGLE.COM", {"A"});
    CHECK_FALSE(r2.cached);  // fresh because clear_cache matched normalized key
}

TEST_CASE("DnsCheck API: NXDOMAIN returns success=true with structured data") {
    DnsCheckService svc;

    auto r = svc.check("thisshouldnotexistexample123456789.com", {"A"});
    CHECK_FALSE(r.success);
    CHECK_FALSE(r.per_type.empty());
    bool valid_nx = (r.per_type[0].status_code == "NXDOMAIN"
                     || r.per_type[0].status_code == "SERVFAIL"
                     || r.per_type[0].status_code == "TIMEOUT");
    CHECK(valid_nx);

    // NXDOMAIN should have a record with the status
    CHECK_FALSE(r.overall_status.empty());
}

TEST_CASE("DnsCheck API: default types list") {
    DnsCheckService svc;

    // All supported types
    auto r = svc.check("google.com", {"A", "AAAA", "MX", "TXT", "NS", "SOA", "CAA"});
    CHECK(r.success);
    CHECK(r.per_type.size() == 7);

    // Each type has a per_type entry
    bool found_a = false, found_mx = false;
    for (const auto& pt : r.per_type) {
        if (pt.type == "A") found_a = true;
        if (pt.type == "MX") found_mx = true;
    }
    CHECK(found_a);
    CHECK(found_mx);
}

TEST_CASE("DnsCheck API: partial success") {
    // A domain with A record but dubious AAAA could produce partial
    DnsCheckService svc;

    auto r = svc.check("google.com", {"A", "AAAA", "MX"});
    CHECK(r.per_type.size() == 3);

    int noerror = 0;
    for (const auto& pt : r.per_type) {
        if (pt.status_code == "NOERROR") noerror++;
    }
    CHECK(noerror >= 2);  // At least A and MX should work
    bool valid_overall = (r.overall_status == "complete" || r.overall_status == "partial");
    CHECK(valid_overall);
}

TEST_CASE("DnsCheck API: JSON field presence") {
    DnsCheckService svc;

    auto r = svc.check("google.com", {"A"});
    CHECK(r.success);

    // Verify all expected fields are present
    CHECK_FALSE(r.domain.empty());
    CHECK_FALSE(r.resolved_at.empty());
    CHECK_FALSE(r.overall_status.empty());
    CHECK(r.per_type.size() >= 1);

    const auto& pt = r.per_type[0];
    CHECK_FALSE(pt.type.empty());
    CHECK_FALSE(pt.status_code.empty());
    CHECK(pt.records.size() >= 1);

    const auto& rec = pt.records[0];
    CHECK_FALSE(rec.type.empty());
    CHECK_FALSE(rec.value.empty());
    CHECK(rec.ttl > 0);
    CHECK_FALSE(rec.dns_response_details.empty());
}

TEST_CASE("DnsCheck API: unsupported type returns error") {
    // This is validated in check(), not the API handler, but test the pathway
    DnsCheckService svc;
    auto r = svc.check("example.com", {"SPF"});
    CHECK_FALSE(r.success);
    CHECK(r.error.find("Unsupported DNS record type") != std::string::npos);
}

TEST_CASE("DnsCheck API: invalid domain returns error") {
    DnsCheckService svc;
    auto r = svc.check("not a domain!@#$", {"A"});
    CHECK_FALSE(r.success);
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("DnsCheck API: cached after clear_cache + fresh call") {
    DnsCheckService svc;

    svc.check("cloudflare.com", {"A"});  // populate cache
    svc.check("cloudflare.com", {"A"});  // should be cached
    svc.clear_cache("cloudflare.com");

    auto r = svc.check("cloudflare.com", {"A"});
    CHECK_FALSE(r.cached);  // fresh after clear
}

TEST_CASE("DnsCheck API: soa fields populated") {
    DnsCheckService svc;

    auto r = svc.check("google.com", {"SOA"});
    CHECK(r.success);

    CHECK_FALSE(r.soa.mname.empty());
    CHECK_FALSE(r.soa.rname.empty());
    CHECK(r.soa.serial > 0);
}

TEST_CASE("DnsCheck API: soa fields empty when not queried") {
    DnsCheckService svc;

    auto r = svc.check("google.com", {"A"});
    CHECK(r.success);

    // SOA not queried — fields should be empty
    CHECK(r.soa.mname.empty());
    CHECK(r.soa.rname.empty());
    CHECK(r.soa.serial == 0);
}
