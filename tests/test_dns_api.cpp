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
// Tracks check() and clear_cache() calls for refresh testing.
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

    // Override clear_cache to track calls and clear fake cache state
    void clear_cache(const std::string& domain) override {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        clear_cache_count_++;
        last_cleared_domain_ = domain;
        cache_flags_.erase(domain);
    }

    // Query and reset tracking
    int clear_cache_count() const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        return clear_cache_count_;
    }

    std::string last_cleared_domain() const {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        return last_cleared_domain_;
    }

    void reset_clear_cache_tracking() {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        clear_cache_count_ = 0;
        last_cleared_domain_.clear();
    }

    // Mark domain as cached or uncached
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
    int clear_cache_count_ = 0;
    std::string last_cleared_domain_;
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

TEST_CASE("API handler: ?refresh=1 bypasses cache via real handler — clear_cache intercepted") {
    FakeDnsCheckService fake;
    fake.set_check_hook([&](const std::string& domain,
                             const std::vector<std::string>& types) {
        (void)types;
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

    // 1. Mark domain as cached
    fake.set_cache_has("refresh-test.com", true);
    fake.reset_clear_cache_tracking();

    // 2. Request WITHOUT refresh — cache is NOT cleared, cached=true returned
    auto r1 = router.dispatch(
        make_req("/api/domains/refresh-test.com/dns-check", "A"));
    CHECK(r1.status_code == 200);
    CHECK(r1.body.find("\"cached\":true") != std::string::npos);
    CHECK(fake.clear_cache_count() == 0);

    // 3. Request WITH refresh=1 — handler calls clear_cache, then check returns cached=false
    auto r2 = router.dispatch(
        make_req("/api/domains/refresh-test.com/dns-check", "A", "1"));
    CHECK(r2.status_code == 200);
    CHECK(r2.body.find("\"cached\":false") != std::string::npos);
    CHECK(fake.clear_cache_count() == 1);
    CHECK(fake.last_cleared_domain() == "refresh-test.com");

    // 4. Verify refresh=0 does NOT call clear_cache
    fake.reset_clear_cache_tracking();
    fake.set_cache_has("refresh-test.com", true);
    auto r3 = router.dispatch(
        make_req("/api/domains/refresh-test.com/dns-check", "A", "0"));
    CHECK(r3.status_code == 200);
    CHECK(fake.clear_cache_count() == 0);
    CHECK(r3.body.find("\"cached\":true") != std::string::npos);
}

TEST_CASE("API handler: missing refresh param does not call clear_cache") {
    FakeDnsCheckService fake;
    fake.set_check_hook([&](const std::string& domain,
                             const std::vector<std::string>& types) {
        (void)types;
        DnsCheckResult r;
        r.domain = domain;
        r.success = true;
        r.overall_status = "complete";
        r.cached = true;
        return r;
    });

    Router router;
    router.add_prefix("GET", "/api/domains/",
        [&fake](const Request& req) {
            return handleDnsCheck(req, fake);
        });

    fake.reset_clear_cache_tracking();
    auto resp = router.dispatch(
        make_req("/api/domains/example.com/dns-check", "A"));
    CHECK(resp.status_code == 200);
    CHECK(fake.clear_cache_count() == 0);
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
        (void)types;
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
        (void)types;
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
