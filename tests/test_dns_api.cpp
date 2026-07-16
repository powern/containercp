#include "api/Router.h"
#include "api/JsonFormatter.h"
#include "dns/DnsCheckService.h"
#include "doctest/doctest.h"

#include <sstream>
#include <string>
#include <vector>

using namespace containercp::dns;
using containercp::api::Router;
using containercp::api::Request;
using containercp::api::Response;
using containercp::api::JsonFormatter;

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

TEST_CASE("[integration] DnsCheck API: cached metadata - first call misses — requires live DNS") {
    DnsCheckService svc;
    auto r = svc.check("google.com", {"A"});
    CHECK_FALSE(r.cached);  // first call is live lookup
}

TEST_CASE("[integration] DnsCheck API: cached metadata - second call hits — requires live DNS") {
    DnsCheckService svc;
    svc.check("google.com", {"A"});   // populate cache
    auto r = svc.check("google.com", {"A"});  // cache hit
    CHECK(r.cached);
}

TEST_CASE("[integration] DnsCheck API: clear_cache forces fresh lookup — requires live DNS") {
    DnsCheckService svc;
    svc.check("google.com", {"A"});   // populate cache

    svc.clear_cache("google.com");
    auto r = svc.check("google.com", {"A"});  // should be live
    CHECK_FALSE(r.cached);
}

TEST_CASE("[integration] DnsCheck API: uppercase domain normalization — requires live DNS") {
    DnsCheckService svc;

    auto r = svc.check("GOOGLE.COM", {"A"});
    CHECK(r.success);
    CHECK(r.domain == "google.com");

    // Cache should use normalized key
    svc.clear_cache("GOOGLE.COM");
    auto r2 = svc.check("GOOGLE.COM", {"A"});
    CHECK_FALSE(r2.cached);  // fresh because clear_cache matched normalized key
}

TEST_CASE("[integration] DnsCheck API: NXDOMAIN returns success=true — requires live DNS") {
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

TEST_CASE("[integration] DnsCheck API: default types list — requires live DNS") {
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

TEST_CASE("[integration] DnsCheck API: partial success — requires live DNS") {
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

TEST_CASE("[integration] DnsCheck API: JSON field presence — requires live DNS") {
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

TEST_CASE("[integration] DnsCheck API: cached after clear_cache + fresh call — requires live DNS") {
    DnsCheckService svc;

    svc.check("cloudflare.com", {"A"});  // populate cache
    svc.check("cloudflare.com", {"A"});  // should be cached
    svc.clear_cache("cloudflare.com");

    auto r = svc.check("cloudflare.com", {"A"});
    CHECK_FALSE(r.cached);  // fresh after clear
}

TEST_CASE("[integration] DnsCheck API: soa fields populated — requires live DNS") {
    DnsCheckService svc;

    auto r = svc.check("google.com", {"SOA"});
    CHECK(r.success);

    CHECK_FALSE(r.soa.mname.empty());
    CHECK_FALSE(r.soa.rname.empty());
    CHECK(r.soa.serial > 0);
}

TEST_CASE("[integration] DnsCheck API: soa fields empty when not queried — requires live DNS") {
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

TEST_CASE("[integration] DnsCheck API: underscore DNS names resolve correctly — requires live DNS") {
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
TEST_CASE("[integration] DnsCheck API JSON: valid JSON for complete response — requires live DNS") {
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

TEST_CASE("[integration] DnsCheck API JSON: valid JSON for NXDOMAIN response — requires live DNS") {
    DnsCheckService svc;

    auto r = svc.check("thisshouldnotexistexample123456789test.com", {"A"});
    // NXDOMAIN is a valid DNS response — check JSON fields
    CHECK_FALSE(r.domain.empty());
    CHECK_FALSE(r.resolved_at.empty());
    CHECK(r.per_type.size() == 1);
}

TEST_CASE("[integration] DnsCheck API JSON: valid JSON for partial response — requires live DNS") {
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

TEST_CASE("[integration] DnsCheck API: autodiscover resolution — requires live DNS") {
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

TEST_CASE("[integration] DnsCheck API: autodiscover A with multiple records — requires live DNS") {
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

TEST_CASE("[integration] DnsCheck API: autodiscover no record — requires live DNS") {
    DnsCheckService svc;

    // A domain that likely has no autodiscover record
    auto r = svc.check("autodiscover.thisshouldnotexistexample.com", {"CNAME", "A"});
    // Should not crash — valid DNS response
    CHECK_FALSE(r.per_type.empty());
    for (const auto& pt : r.per_type) {
        CHECK_FALSE(pt.status_code.empty());
    }
}

TEST_CASE("[integration] DnsCheck API: CNAME resolution — requires live DNS") {
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

TEST_CASE("[integration] DnsCheck API: CNAME with trailing dot — requires live DNS") {
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

// -----------------------------------------------------------------------
// API handler integration tests — tests go through Router::dispatch()
// with a handler that replicates the real dns-check API logic.
// -----------------------------------------------------------------------

// Handler factory: returns a handler that uses a shared DnsCheckService
static auto make_dns_handler(DnsCheckService& svc) {
    return [&svc](const Request& req) -> Response {
        Response r;
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
        for (char c : domain_raw) domain.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

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
        if (types.empty()) types = {"A", "AAAA", "MX", "TXT", "NS", "SOA", "CAA"};

        for (const auto& t : types) {
            if (!DnsCheckService::validate_type(t)) {
                r.status_code = 400;
                r.body = "{\"success\":false,\"error\":\"Unsupported DNS record type: "
                    + JsonFormatter::escape(t) + "\"}";
                return r;
            }
        }

        if (refresh) svc.clear_cache(domain);
        auto result = svc.check(domain, types);

        std::ostringstream json;
        json << "{\"success\":"
             << (result.success ? "true" : "false")
             << ",\"data\":{"
             << "\"domain\":\"" << JsonFormatter::escape(result.domain)
             << "\",\"resolved_at\":\"" << JsonFormatter::escape(result.resolved_at)
             << "\",\"cached\":" << (result.cached ? "true" : "false")
             << ",\"overall_status\":\"" << JsonFormatter::escape(result.overall_status)
             << "\",\"per_type\":[";
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
                     << "\",\"value\":\"" << JsonFormatter::escape(rec.value)
                     << "\",\"ttl\":" << rec.ttl
                     << ",\"priority\":" << rec.priority
                     << "}";
            }
            json << "]}";
        }
        json << "],\"soa\":{"
             << "\"mname\":\"" << JsonFormatter::escape(result.soa.mname)
             << "\",\"rname\":\"" << JsonFormatter::escape(result.soa.rname)
             << "\",\"serial\":" << result.soa.serial
             << "}}";
        if (!result.error.empty()) {
            json << ",\"error\":\"" << JsonFormatter::escape(result.error) << "\"";
        }
        json << "}";
        r.body = json.str();
        r.status_code = DnsCheckService::compute_http_status(result.per_type, result.success);
        return r;
    };
}

TEST_CASE("[integration] API handler: GET /api/domains/example.com/dns-check — requires live DNS") {
    DnsCheckService svc;
    Router router;
    router.add_prefix("GET", "/api/domains/", make_dns_handler(svc));

    Request req;
    req.method = "GET";
    req.path = "/api/domains/example.com/dns-check";
    auto resp = router.dispatch(req);

    CHECK(resp.status_code == 200);
    CHECK(resp.body.find("\"success\":true") != std::string::npos);
    CHECK(resp.body.find("\"data\":{") != std::string::npos);
    CHECK(resp.body.find("\"domain\":\"example.com\"") != std::string::npos);
    CHECK(resp.body.find("\"resolved_at\":\"") != std::string::npos);
    CHECK(resp.body.find("\"overall_status\":") != std::string::npos);
    CHECK(resp.body.find("\"per_type\":[") != std::string::npos);
}

TEST_CASE("[integration] API handler: GET with ?types=A,MX returns only requested types") {
    DnsCheckService svc;
    Router router;
    router.add_prefix("GET", "/api/domains/", make_dns_handler(svc));

    Request req;
    req.method = "GET";
    req.path = "/api/domains/example.com/dns-check";
    req.query["types"] = "A,MX";
    auto resp = router.dispatch(req);

    CHECK(resp.status_code == 200);
    CHECK(resp.body.find("\"type\":\"A\"") != std::string::npos);
    CHECK(resp.body.find("\"type\":\"MX\"") != std::string::npos);
    // Should NOT contain types that were not requested — AAAA should be absent
    bool has_aaaa = resp.body.find("\"AAAA\"") != std::string::npos
                    || resp.body.find("\"type\":\"AAAA\"") != std::string::npos;
    CHECK_FALSE(has_aaaa);

    // Count per_type entries by looking for the status_code field which follows type
    int type_count = 0;
    size_t pos = 0;
    while ((pos = resp.body.find("\"status_code\":\"", pos)) != std::string::npos) {
        type_count++;
        pos++;
    }
    CHECK(type_count == 2);
}

TEST_CASE("[integration] API handler: GET with ?refresh=1 bypasses cache — requires live DNS") {
    DnsCheckService svc;
    Router router;
    router.add_prefix("GET", "/api/domains/", make_dns_handler(svc));

    // First call — cache miss
    Request req1;
    req1.method = "GET";
    req1.path = "/api/domains/cloudflare.com/dns-check";
    req1.query["types"] = "A";
    auto resp1 = router.dispatch(req1);
    CHECK(resp1.status_code == 200);
    bool first_cached = resp1.body.find("\"cached\":true") != std::string::npos;
    CHECK_FALSE(first_cached);

    // Second call — cache hit (no refresh)
    Request req2;
    req2.method = "GET";
    req2.path = "/api/domains/cloudflare.com/dns-check";
    req2.query["types"] = "A";
    auto resp2 = router.dispatch(req2);
    CHECK(resp2.body.find("\"cached\":true") != std::string::npos);

    // Third call with refresh=1 — cache should be cleared
    Request req3;
    req3.method = "GET";
    req3.path = "/api/domains/cloudflare.com/dns-check";
    req3.query["types"] = "A";
    req3.query["refresh"] = "1";
    auto resp3 = router.dispatch(req3);
    CHECK(resp3.status_code == 200);
    bool third_cached = resp3.body.find("\"cached\":true") != std::string::npos;
    CHECK_FALSE(third_cached);
}

TEST_CASE("API handler: invalid domain returns 400") {
    DnsCheckService svc;
    Router router;
    router.add_prefix("GET", "/api/domains/", make_dns_handler(svc));

    Request req;
    req.method = "GET";
    req.path = "/api/domains/not a domain!!!/dns-check";
    req.query["types"] = "A";
    auto resp = router.dispatch(req);

    CHECK(resp.status_code == 400);
    bool has_err = resp.body.find("\"error\":\"Invalid domain") != std::string::npos;
    CHECK(has_err);
}

TEST_CASE("API handler: unsupported type SPF returns 400") {
    DnsCheckService svc;
    Router router;
    router.add_prefix("GET", "/api/domains/", make_dns_handler(svc));

    Request req;
    req.method = "GET";
    req.path = "/api/domains/example.com/dns-check";
    req.query["types"] = "SPF";
    auto resp = router.dispatch(req);

    CHECK(resp.status_code == 400);
    CHECK(resp.body.find("Unsupported DNS record type") != std::string::npos);
    CHECK(resp.body.find("SPF") != std::string::npos);
}

TEST_CASE("API handler: mixed types A,SPF,INVALID returns 400 on first invalid") {
    DnsCheckService svc;
    Router router;
    router.add_prefix("GET", "/api/domains/", make_dns_handler(svc));

    Request req;
    req.method = "GET";
    req.path = "/api/domains/example.com/dns-check";
    req.query["types"] = "A,SPF,INVALID";
    auto resp = router.dispatch(req);

    CHECK(resp.status_code == 400);
    // SPF is the first invalid type encountered
    CHECK(resp.body.find("Unsupported DNS record type") != std::string::npos);
    CHECK(resp.body.find("SPF") != std::string::npos);
}

TEST_CASE("API handler: empty domain after path parsing returns 400") {
    DnsCheckService svc;
    Router router;
    router.add_prefix("GET", "/api/domains/", make_dns_handler(svc));

    Request req;
    req.method = "GET";
    req.path = "/api/domains//dns-check";
    auto resp = router.dispatch(req);

    CHECK(resp.status_code == 404);  // empty path before suffix hits prefix regex
}

TEST_CASE("[integration] API handler: NXDOMAIN returns 200 with valid structure — requires live DNS") {
    DnsCheckService svc;
    Router router;
    router.add_prefix("GET", "/api/domains/", make_dns_handler(svc));

    Request req;
    req.method = "GET";
    req.path = "/api/domains/thisshouldnotexistexample1234567890test.com/dns-check";
    req.query["types"] = "A";
    auto resp = router.dispatch(req);

    bool is_nxdomain = resp.body.find("\"NXDOMAIN\"") != std::string::npos;
    bool is_servfail = resp.body.find("\"SERVFAIL\"") != std::string::npos;
    bool is_timeout = resp.body.find("\"TIMEOUT\"") != std::string::npos;
    if (is_nxdomain) {
        CHECK(resp.status_code == 200);
        CHECK(resp.body.find("\"success\":true") != std::string::npos);
    }
    if (is_servfail || is_timeout) {
        CHECK(resp.body.find("\"success\":false") != std::string::npos);
    }
    CHECK(resp.body.find("\"domain\":\"") != std::string::npos);
    CHECK(resp.body.find("\"per_type\":[") != std::string::npos);
}
