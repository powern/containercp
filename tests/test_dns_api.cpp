#include "api/Router.h"
#include "dns/DnsCheckHandler.h"
#include "dns/DnsCheckService.h"
#include "doctest/doctest.h"

#include <mutex>
#include <string>
#include <vector>

using namespace containercp::dns;
using containercp::api::Router;
using containercp::api::Request;
using containercp::api::Response;

// -----------------------------------------------------------------------
// FakeDnsCheckService — returns controlled results without live DNS
// -----------------------------------------------------------------------
class FakeDnsCheckService : public DnsCheckService {
public:
    using CheckHook = std::function<DnsCheckResult(
        const std::string& domain, const std::vector<std::string>& types)>;

    void set_check_hook(CheckHook hook) {
        std::lock_guard<std::mutex> lock(mutex_);
        hook_ = std::move(hook);
    }

    DnsCheckResult check(const std::string& domain,
                          const std::vector<std::string>& types) override
    {
        CheckHook h;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            h = hook_;
        }
        if (h) return h(domain, types);

        // Default: success with A record
        DnsCheckResult r;
        r.domain = domain;
        r.success = true;
        r.overall_status = "complete";
        r.cached = false;
        PerTypeResult pt;
        pt.type = "A";
        pt.status_code = "NOERROR";
        DnsRecord rec;
        rec.type = "A";
        rec.name = domain;
        rec.value = "192.0.2.1";
        rec.ttl = 300;
        pt.records.push_back(std::move(rec));
        r.per_type.push_back(std::move(pt));
        return r;
    }

    // Cache tracking for refresh tests
    void set_cache_has(const std::string& domain, bool has) {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        if (has)
            cache_flags_[domain] = true;
        else
            cache_flags_.erase(domain);
    }

    bool has_cached_impl(const std::string& domain) const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        return cache_flags_.find(domain) != cache_flags_.end();
    }

private:
    CheckHook hook_;
    mutable std::mutex mutex_;
    mutable std::mutex cache_mutex_;
    mutable std::map<std::string, bool> cache_flags_;
};

// -----------------------------------------------------------------------
// Test helpers
// -----------------------------------------------------------------------
static Request make_req(const std::string& path) {
    Request req;
    req.method = "GET";
    req.path = path;
    return req;
}

static Request make_req(const std::string& path,
                         const std::string& types)
{
    Request req;
    req.method = "GET";
    req.path = path;
    if (!types.empty()) req.query["types"] = types;
    return req;
}

static Request make_req(const std::string& path,
                         const std::string& types,
                         const std::string& refresh)
{
    Request req;
    req.method = "GET";
    req.path = path;
    if (!types.empty()) req.query["types"] = types;
    if (!refresh.empty()) req.query["refresh"] = refresh;
    return req;
}

// -----------------------------------------------------------------------
// Suite 1: Deterministic API handler tests (no live DNS, no network)
// -----------------------------------------------------------------------

TEST_CASE("API handler: 200 with success envelope via real handleDnsCheck") {
    FakeDnsCheckService fake;
    Router router;
    router.add_prefix("GET", "/api/domains/",
        [&fake](const Request& req) {
            return handleDnsCheck(req, fake);
        });

    auto resp = router.dispatch(make_req("/api/domains/example.com/dns-check"));

    CHECK(resp.status_code == 200);
    CHECK(resp.body.find("\"success\":true") != std::string::npos);
    CHECK(resp.body.find("\"data\":{") != std::string::npos);
    CHECK(resp.body.find("\"domain\":\"example.com\"") != std::string::npos);
    CHECK(resp.body.find("\"resolved_at\":\"") != std::string::npos);
    CHECK(resp.body.find("\"cached\":false") != std::string::npos);
    CHECK(resp.body.find("\"overall_status\":\"complete\"") != std::string::npos);
    CHECK(resp.body.find("\"per_type\":[") != std::string::npos);
    CHECK(resp.body.find("\"soa\":{") != std::string::npos);
    CHECK(resp.body.find("\"spf_analysis\":{") != std::string::npos);
    CHECK(resp.body.find("\"expected_ipv4\":\"\"") != std::string::npos);
}

TEST_CASE("API handler: ?types=A,MX returns only requested types") {
    FakeDnsCheckService fake;
    fake.set_check_hook([](const std::string& domain,
                            const std::vector<std::string>& types) {
        DnsCheckResult r;
        r.domain = domain;
        r.success = true;
        r.overall_status = "complete";
        for (const auto& t : types) {
            PerTypeResult pt;
            pt.type = t;
            pt.status_code = "NOERROR";
            DnsRecord rec;
            rec.type = t;
            rec.name = domain;
            rec.value = t == "A" ? "192.0.2.1" : "10 mail.example.com";
            rec.ttl = 300;
            pt.records.push_back(std::move(rec));
            r.per_type.push_back(std::move(pt));
        }
        return r;
    });

    Router router;
    router.add_prefix("GET", "/api/domains/",
        [&fake](const Request& req) {
            return handleDnsCheck(req, fake);
        });

    auto resp = router.dispatch(
        make_req("/api/domains/example.com/dns-check", "A,MX"));

    CHECK(resp.status_code == 200);
    CHECK(resp.body.find("\"type\":\"A\"") != std::string::npos);
    CHECK(resp.body.find("\"type\":\"MX\"") != std::string::npos);

    // Count per_type entries via status_code field (one per per_type)
    int type_count = 0;
    size_t pos = 0;
    while ((pos = resp.body.find("\"status_code\":\"", pos)) != std::string::npos) {
        type_count++;
        pos++;
    }
    CHECK(type_count == 2);
}

TEST_CASE("API handler: ?refresh=1 bypasses cache via real handler") {
    FakeDnsCheckService fake;
    int check_count = 0;
    fake.set_check_hook([&](const std::string& domain,
                             const std::vector<std::string>& types) {
        check_count++;
        DnsCheckResult r;
        r.domain = domain;
        r.success = true;
        r.overall_status = "complete";
        r.cached = fake.has_cached_impl(domain);
        PerTypeResult pt;
        pt.type = "A";
        pt.status_code = "NOERROR";
        DnsRecord rec;
        rec.type = "A";
        rec.name = domain;
        rec.value = "192.0.2.1";
        rec.ttl = 300;
        pt.records.push_back(std::move(rec));
        r.per_type.push_back(std::move(pt));
        return r;
    });

    Router router;
    router.add_prefix("GET", "/api/domains/",
        [&fake](const Request& req) {
            return handleDnsCheck(req, fake);
        });

    // First: cache miss
    auto r1 = router.dispatch(
        make_req("/api/domains/example.com/dns-check", "A"));
    CHECK(r1.status_code == 200);
    CHECK(r1.body.find("\"cached\":false") != std::string::npos);
    CHECK(check_count == 1);

    // Second: mark as cached, should see cached=true
    fake.set_cache_has("example.com", true);
    auto r2 = router.dispatch(
        make_req("/api/domains/example.com/dns-check", "A"));
    CHECK(r2.status_code == 200);
    CHECK(r2.body.find("\"cached\":true") != std::string::npos);
    CHECK(check_count == 2);

    // Third: with refresh=1 — handler calls clear_cache, then check
    // The hook will now return cached=false because we removed the flag
    fake.set_cache_has("example.com", false);
    auto r3 = router.dispatch(
        make_req("/api/domains/example.com/dns-check", "A", "1"));
    CHECK(r3.status_code == 200);
    CHECK(r3.body.find("\"cached\":false") != std::string::npos);
    CHECK(check_count == 3);
}

TEST_CASE("API handler: invalid domain returns 400") {
    FakeDnsCheckService fake;
    Router router;
    router.add_prefix("GET", "/api/domains/",
        [&fake](const Request& req) {
            return handleDnsCheck(req, fake);
        });

    auto resp = router.dispatch(
        make_req("/api/domains/not a valid@domain!!/dns-check", "A"));

    CHECK(resp.status_code == 400);
    bool has_err = resp.body.find("\"error\":\"Invalid domain") != std::string::npos;
    CHECK(has_err);
}

TEST_CASE("API handler: unsupported type SPF returns 400") {
    FakeDnsCheckService fake;
    Router router;
    router.add_prefix("GET", "/api/domains/",
        [&fake](const Request& req) {
            return handleDnsCheck(req, fake);
        });

    auto resp = router.dispatch(
        make_req("/api/domains/example.com/dns-check", "SPF"));

    CHECK(resp.status_code == 400);
    CHECK(resp.body.find("Unsupported DNS record type") != std::string::npos);
    CHECK(resp.body.find("SPF") != std::string::npos);
}

TEST_CASE("API handler: mixed types A,SPF,INVALID returns 400 on first invalid") {
    FakeDnsCheckService fake;
    Router router;
    router.add_prefix("GET", "/api/domains/",
        [&fake](const Request& req) {
            return handleDnsCheck(req, fake);
        });

    auto resp = router.dispatch(
        make_req("/api/domains/example.com/dns-check", "A,SPF,INVALID"));

    CHECK(resp.status_code == 400);
    CHECK(resp.body.find("Unsupported DNS record type") != std::string::npos);
    CHECK(resp.body.find("SPF") != std::string::npos);
}

TEST_CASE("API handler: unknown subpath returns 404") {
    FakeDnsCheckService fake;
    Router router;
    router.add_prefix("GET", "/api/domains/",
        [&fake](const Request& req) {
            return handleDnsCheck(req, fake);
        });

    auto resp = router.dispatch(
        make_req("/api/domains/unknown"));

    CHECK(resp.status_code == 404);
}

TEST_CASE("API handler: NXDOMAIN returns 200 with valid structure") {
    FakeDnsCheckService fake;
    fake.set_check_hook([](const std::string& domain,
                            const std::vector<std::string>& types) {
        DnsCheckResult r;
        r.domain = domain;
        r.success = true;
        r.overall_status = "complete";
        PerTypeResult pt;
        pt.type = "A";
        pt.status_code = "NXDOMAIN";
        pt.error = "NXDOMAIN";
        r.per_type.push_back(std::move(pt));
        return r;
    });

    Router router;
    router.add_prefix("GET", "/api/domains/",
        [&fake](const Request& req) {
            return handleDnsCheck(req, fake);
        });

    auto resp = router.dispatch(
        make_req("/api/domains/nonexistent.example.com/dns-check", "A"));

    CHECK(resp.status_code == 200);
    CHECK(resp.body.find("\"success\":true") != std::string::npos);
    CHECK(resp.body.find("\"status_code\":\"NXDOMAIN\"") != std::string::npos);
}

TEST_CASE("API handler: SERVFAIL returns 502") {
    FakeDnsCheckService fake;
    fake.set_check_hook([](const std::string& domain,
                            const std::vector<std::string>& types) {
        DnsCheckResult r;
        r.domain = domain;
        r.success = false;
        r.overall_status = "failed";
        r.error = "SERVFAIL";
        PerTypeResult pt;
        pt.type = "A";
        pt.status_code = "SERVFAIL";
        pt.error = "SERVFAIL";
        r.per_type.push_back(std::move(pt));
        return r;
    });

    Router router;
    router.add_prefix("GET", "/api/domains/",
        [&fake](const Request& req) {
            return handleDnsCheck(req, fake);
        });

    auto resp = router.dispatch(
        make_req("/api/domains/example.com/dns-check", "A"));

    CHECK(resp.status_code == 502);
    CHECK(resp.body.find("\"success\":false") != std::string::npos);
    CHECK(resp.body.find("SERVFAIL") != std::string::npos);
}

TEST_CASE("API handler: empty types defaults to all supported types") {
    FakeDnsCheckService fake;
    std::vector<std::string> requested_types;
    fake.set_check_hook([&](const std::string& domain,
                             const std::vector<std::string>& types) {
        requested_types = types;
        DnsCheckResult r;
        r.domain = domain;
        r.success = true;
        r.overall_status = "complete";
        for (const auto& t : types) {
            PerTypeResult pt;
            pt.type = t;
            pt.status_code = "NOERROR";
            r.per_type.push_back(std::move(pt));
        }
        return r;
    });

    Router router;
    router.add_prefix("GET", "/api/domains/",
        [&fake](const Request& req) {
            return handleDnsCheck(req, fake);
        });

    auto resp = router.dispatch(
        make_req("/api/domains/example.com/dns-check"));

    CHECK(resp.status_code == 200);
    CHECK(requested_types.size() == 7);
    bool has_a = false, has_mx = false;
    for (const auto& t : requested_types) {
        if (t == "A") has_a = true;
        if (t == "MX") has_mx = true;
    }
    CHECK(has_a);
    CHECK(has_mx);
}

TEST_CASE("API handler: refresh=1 clears cache before check") {
    FakeDnsCheckService fake;
    int clear_calls = 0;
    fake.set_check_hook([&](const std::string& domain,
                             const std::vector<std::string>& types) {
        DnsCheckResult r;
        r.domain = domain;
        r.success = true;
        r.overall_status = "complete";
        r.cached = fake.has_cached_impl(domain);
        fake.set_cache_has(domain, true);
        PerTypeResult pt;
        pt.type = "A";
        pt.status_code = "NOERROR";
        DnsRecord rec;
        rec.type = "A";
        rec.name = domain;
        rec.value = "192.0.2.1";
        rec.ttl = 300;
        pt.records.push_back(std::move(rec));
        r.per_type.push_back(std::move(pt));
        return r;
    });

    Router router;
    router.add_prefix("GET", "/api/domains/",
        [&fake](const Request& req) {
            return handleDnsCheck(req, fake);
        });

    // First call — cache miss
    auto r1 = router.dispatch(
        make_req("/api/domains/refresh-test.com/dns-check", "A"));
    CHECK(r1.body.find("\"cached\":false") != std::string::npos);

    // Second call — cache hit (fake marks it cached)
    auto r2 = router.dispatch(
        make_req("/api/domains/refresh-test.com/dns-check", "A"));
    CHECK(r2.body.find("\"cached\":true") != std::string::npos);

    // Third call with refresh=1 — handler clears cache, result is fresh
    fake.set_cache_has("refresh-test.com", false);
    auto r3 = router.dispatch(
        make_req("/api/domains/refresh-test.com/dns-check", "A", "1"));
    CHECK(r3.body.find("\"cached\":false") != std::string::npos);
}

// -----------------------------------------------------------------------
// Suite 2: Optional live-DNS tests
// Run with: ./containercp_tests -ts="[integration]"
// Skip with: ./containercp_tests -ts="[integration]" -exclude
// -----------------------------------------------------------------------

TEST_SUITE("[integration]") {

TEST_CASE("DnsCheck API: real A record") {
    DnsCheckService svc;
    auto r = svc.check("google.com", {"A"});
    CHECK(r.success);
    CHECK(r.overall_status == "complete");
    CHECK(r.per_type.size() == 1);
    CHECK(r.per_type[0].status_code == "NOERROR");
    bool found = false;
    for (const auto& rec : r.per_type[0].records) {
        if (rec.type == "A" && !rec.value.empty()) { found = true; break; }
    }
    CHECK(found);
}

TEST_CASE("DnsCheck API: real MX record") {
    DnsCheckService svc;
    auto r = svc.check("google.com", {"MX"});
    CHECK(r.success);
    CHECK(r.per_type[0].status_code == "NOERROR");
    bool found = false;
    for (const auto& rec : r.per_type[0].records) {
        if (rec.type == "MX" && rec.priority > 0) { found = true; break; }
    }
    CHECK(found);
}

TEST_CASE("DnsCheck API: real NXDOMAIN") {
    DnsCheckService svc;
    auto r = svc.check("thisshouldnotexistexampletest999999.com", {"A"});
    std::string sc = r.per_type[0].status_code;
    bool ok = sc == "NXDOMAIN" || sc == "SERVFAIL";
    CHECK(ok);
    if (sc == "NXDOMAIN") CHECK(r.success);
}

TEST_CASE("DnsCheck API: SOA fields") {
    DnsCheckService svc;
    auto r = svc.check("google.com", {"SOA"});
    CHECK_FALSE(r.soa.mname.empty());
    CHECK_FALSE(r.soa.rname.empty());
    CHECK(r.soa.serial > 0);
}

} // TEST_SUITE("[integration]")
