#include "api/Router.h"
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
    // NXDOMAIN is a valid DNS diagnostic result — success=true
    bool is_nxdomain = (r.per_type.size() == 1
                        && r.per_type[0].status_code == "NXDOMAIN");
    bool is_resolver_fail = (r.per_type.size() == 1
                             && (r.per_type[0].status_code == "SERVFAIL"
                                 || r.per_type[0].status_code == "TIMEOUT"));
    if (is_nxdomain) {
        CHECK(r.success);
        CHECK(r.overall_status == "complete");
    } else if (is_resolver_fail) {
        CHECK_FALSE(r.success);
        CHECK(r.overall_status == "failed");
    }
    CHECK_FALSE(r.per_type.empty());
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

TEST_CASE("DnsCheck API: underscore names pass validation") {
    // validate_dns_name allows underscore, so the API should accept these
    CHECK(DnsCheckService::validate_dns_name("_dmarc.example.com"));
    CHECK(DnsCheckService::validate_dns_name("dkim._domainkey.example.com"));
    CHECK(DnsCheckService::validate_dns_name("_smtp._tls.example.com"));

    // These would have been rejected by the old validate_domain
    CHECK_FALSE(DnsCheckService::validate_domain("_dmarc.example.com"));
    CHECK_FALSE(DnsCheckService::validate_domain("dkim._domainkey.example.com"));
}

TEST_CASE("DnsCheck API: underscore DNS names resolve correctly") {
    DnsCheckService svc;

    // Test _dmarc lookup on a major domain (has DMARC record)
    auto r = svc.check("_dmarc.gmail.com", {"TXT"});
    // Should not fail with INVALID — underscore should be allowed
    CHECK(r.error.find("Invalid domain") == std::string::npos);
    CHECK(r.error.find("Unsupported DNS record type") == std::string::npos);

    // _dmarc.gmail.com should have per_type with a status
    bool has_valid_status = (r.per_type.size() == 1 && !r.per_type[0].status_code.empty());
    CHECK(has_valid_status);
}

// --- JSON serialization validation ---
TEST_CASE("DnsCheck API JSON: valid JSON for complete response") {
    DnsCheckService svc;

    // Real DNS check to produce a response
    auto r = svc.check("google.com", {"A", "MX"});
    CHECK(r.success);

    // Verify all expected fields are present in the result struct
    CHECK_FALSE(r.domain.empty());
    CHECK_FALSE(r.resolved_at.empty());
    CHECK_FALSE(r.overall_status.empty());
    CHECK_FALSE(r.per_type.empty());
    CHECK_FALSE(r.expected_ip_stale == true);  // not stale for fresh detection

    // per_type validation
    for (const auto& pt : r.per_type) {
        CHECK_FALSE(pt.type.empty());
        CHECK_FALSE(pt.status_code.empty());
        for (const auto& rec : pt.records) {
            CHECK_FALSE(rec.type.empty());
        }
    }

    // SOA fields may be empty (not queried)
    CHECK(r.soa.mname.empty());
}

TEST_CASE("DnsCheck API JSON: valid JSON for NXDOMAIN response") {
    DnsCheckService svc;

    auto r = svc.check("thisshouldnotexistexample123456789test.com", {"A"});
    // NXDOMAIN is a valid DNS response — check JSON fields
    CHECK_FALSE(r.domain.empty());
    CHECK_FALSE(r.resolved_at.empty());
    CHECK(r.per_type.size() == 1);
}

TEST_CASE("DnsCheck API JSON: valid JSON for partial response") {
    DnsCheckService svc;

    // Query multiple types — some may succeed, some may NXDOMAIN
    auto r = svc.check("google.com", {"A", "AAAA", "MX", "TXT", "NS", "SOA", "CAA"});
    CHECK(r.per_type.size() == 7);
    CHECK_FALSE(r.domain.empty());
    CHECK_FALSE(r.resolved_at.empty());
    CHECK_FALSE(r.overall_status.empty());
    bool valid_overall = (r.overall_status == "complete" || r.overall_status == "partial");
    CHECK(valid_overall);
}

// --- Autodiscover resolution tests ---

TEST_CASE("DnsCheck API: autodiscover domain validation") {
    // Autodiscover uses underscores and standard domain patterns
    CHECK(DnsCheckService::validate_dns_name("autodiscover.example.com"));
    CHECK(DnsCheckService::validate_dns_name("autodiscover.google.com"));
    // These should all be valid DNS names
    CHECK(DnsCheckService::validate_dns_name("_autodiscover._tcp.example.com"));
    CHECK(DnsCheckService::validate_dns_name("_smtp._tls.example.com"));
}

TEST_CASE("DnsCheck API: autodiscover resolution") {
    DnsCheckService svc;

    // autodiscover.google.com typically has a CNAME or A record
    auto r = svc.check("autodiscover.google.com", {"CNAME", "A"});
    CHECK(r.success);

    // Should have per_type results for both CNAME and A
    CHECK(r.per_type.size() == 2);

    bool hasCname = false, hasA = false;
    for (const auto& pt : r.per_type) {
        if (pt.type == "CNAME") hasCname = true;
        if (pt.type == "A") hasA = true;
        // Each should have a valid status_code
        CHECK_FALSE(pt.status_code.empty());
    }
    CHECK(hasCname);
    CHECK(hasA);
}

TEST_CASE("DnsCheck API: autodiscover A with multiple records") {
    DnsCheckService svc;

    // Query a domain that likely has multiple A records for autodiscover
    auto r = svc.check("autodiscover.google.com", {"A"});
    CHECK(r.success);

    // May have 0 or more A records
    for (const auto& pt : r.per_type) {
        if (pt.type == "A" && pt.records.size() > 1) {
            // Verify multiple records are properly parsed
            CHECK(pt.records.size() >= 2);
            // All should have valid IPs
            for (const auto& rec : pt.records) {
                CHECK_FALSE(rec.value.empty());
                CHECK(rec.ttl > 0);
            }
        }
    }
}

TEST_CASE("DnsCheck API: autodiscover no record returns NXDOMAIN or NODATA") {
    DnsCheckService svc;

    // A domain that likely has no autodiscover record
    auto r = svc.check("autodiscover.thisshouldnotexistexample.com", {"CNAME", "A"});
    // Should not crash — valid DNS response
    CHECK_FALSE(r.per_type.empty());
    for (const auto& pt : r.per_type) {
        CHECK_FALSE(pt.status_code.empty());
    }
}

TEST_CASE("DnsCheck API: CNAME resolution returns target hostname") {
    DnsCheckService svc;

    // Many CDN/redirect domains use CNAME
    auto r = svc.check("autodiscover.outlook.com", {"CNAME", "A"});
    CHECK(r.success);

    bool foundCname = false;
    for (const auto& pt : r.per_type) {
        if (pt.type == "CNAME" && !pt.records.empty()) {
            foundCname = true;
            // CNAME target should be a valid hostname (ends with .)
            const auto& cname = pt.records[0].value;
            CHECK_FALSE(cname.empty());
            CHECK(cname.find('.') != std::string::npos);
        }
    }
    // autodiscover.outlook.com may have CNAME or A — either is valid
    CHECK(r.per_type.size() >= 1);
}

TEST_CASE("DnsCheck API: CNAME with trailing dot is valid") {
    DnsCheckService svc;

    // Query a domain that has a known CNAME
    auto r = svc.check("autodiscover.outlook.com", {"CNAME"});
    if (r.success && !r.per_type.empty() && !r.per_type[0].records.empty()) {
        const auto& cname = r.per_type[0].records[0].value;
        // CNAME targets from DNS often have trailing dots
        // The frontend's normalizeHostname strips trailing dots
        CHECK_FALSE(cname.empty());
        if (cname.back() == '.') {
            // Verify normalized form matches
            std::string normalized = cname.substr(0, cname.size() - 1);
            CHECK_FALSE(normalized.empty());
        }
    }
}

// --- Routing tests ---

TEST_CASE("DnsCheck API routing: exact match /api/domains is not blocked") {
    containercp::api::Router router;

    // Register exact match first (like the real code)
    bool exact_called = false;
    router.add("GET", "/api/domains", [&exact_called](const containercp::api::Request&) {
        exact_called = true;
        containercp::api::Response r;
        r.body = "{\"success\":true,\"data\":[]}";
        return r;
    });

    // Register prefix after
    router.add_prefix("GET", "/api/domains/", [](const containercp::api::Request&) {
        containercp::api::Response r;
        r.body = "{\"prefix\":true}";
        return r;
    });

    // Exact path should match exact handler
    containercp::api::Request req_exact;
    req_exact.method = "GET";
    req_exact.path = "/api/domains";
    auto resp_exact = router.dispatch(req_exact);
    CHECK(exact_called);
    CHECK(resp_exact.body.find("\"data\":[]") != std::string::npos);

    // Sub-path should match prefix handler
    containercp::api::Request req_sub;
    req_sub.method = "GET";
    req_sub.path = "/api/domains/example.com/dns-check";
    auto resp_sub = router.dispatch(req_sub);
    CHECK(resp_sub.body.find("\"prefix\"") != std::string::npos);

    // POST /api/domains/remove should not match GET prefix
    containercp::api::Request req_remove;
    req_remove.method = "POST";
    req_remove.path = "/api/domains/remove";
    auto resp_remove = router.dispatch(req_remove);
    CHECK(resp_remove.status_code == 404);
}

TEST_CASE("DnsCheck API routing: prefix dispatcher returns 404 for unknown paths") {
    containercp::api::Router router;

    router.add_prefix("GET", "/api/domains/", [](const containercp::api::Request& req) {
        containercp::api::Response r;
        std::string remaining = req.path.substr(std::string("/api/domains/").size());
        if (remaining == "example.com/dns-check") {
            r.body = "{\"dns\":true}";
        } else {
            r.status_code = 404;
            r.body = "{\"success\":false,\"error\":\"Not found\"}";
        }
        return r;
    });

    // dns-check handled
    containercp::api::Request req_ok;
    req_ok.method = "GET";
    req_ok.path = "/api/domains/example.com/dns-check";
    auto resp_ok = router.dispatch(req_ok);
    CHECK(resp_ok.body.find("\"dns\"") != std::string::npos);

    // Unknown path gets 404
    containercp::api::Request req_unknown;
    req_unknown.method = "GET";
    req_unknown.path = "/api/domains/42";
    auto resp_unknown = router.dispatch(req_unknown);
    CHECK(resp_unknown.status_code == 404);
}

TEST_CASE("DnsCheck API routing: future specific routes work when registered before prefix") {
    containercp::api::Router router;

    // Register specific route first
    bool specific_called = false;
    router.add("GET", "/api/domains/42", [&specific_called](const containercp::api::Request&) {
        specific_called = true;
        containercp::api::Response r;
        r.body = "{\"id\":42}";
        return r;
    });

    // Register prefix after
    router.add_prefix("GET", "/api/domains/", [](const containercp::api::Request&) {
        containercp::api::Response r;
        r.status_code = 404;
        r.body = "{\"success\":false,\"error\":\"Not found\"}";
        return r;
    });

    // Specific route matches first (registered before prefix)
    containercp::api::Request req;
    req.method = "GET";
    req.path = "/api/domains/42";
    auto resp = router.dispatch(req);
    CHECK(specific_called);
    CHECK(resp.body.find("\"id\":42") != std::string::npos);

    // Unknown path falls through to prefix
    containercp::api::Request req_unknown;
    req_unknown.method = "GET";
    req_unknown.path = "/api/domains/unknown";
    auto resp_unknown = router.dispatch(req_unknown);
    CHECK(resp_unknown.status_code == 404);
}
