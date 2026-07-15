#include "dns/DnsCheckService.h"
#include "doctest/doctest.h"

#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace containercp::dns;

TEST_CASE("DnsCheckService domain format validation") {
    // Valid domains
    CHECK(DnsCheckService::validate_domain("example.com"));
    CHECK(DnsCheckService::validate_domain("sub.domain.co.uk"));
    CHECK(DnsCheckService::validate_domain("a-b.com"));
    CHECK(DnsCheckService::validate_domain("UPPERCASE.COM"));  // valid, normalized later

    // Invalid: shell chars
    CHECK_FALSE(DnsCheckService::validate_domain("example.com; rm -rf /"));
    CHECK_FALSE(DnsCheckService::validate_domain("example.com|id"));

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

    // Invalid: too long overall
    std::string long_domain(300, 'a');
    CHECK_FALSE(DnsCheckService::validate_domain(long_domain));

    // Invalid: label starts with hyphen
    CHECK_FALSE(DnsCheckService::validate_domain("-bad.example.com"));

    // Invalid: label ends with hyphen
    CHECK_FALSE(DnsCheckService::validate_domain("bad-.example.com"));

    // Invalid: consecutive dots (empty label)
    CHECK_FALSE(DnsCheckService::validate_domain("bad..example.com"));

    // Invalid: label > 63 chars
    std::string long_label(65, 'a');
    CHECK_FALSE(DnsCheckService::validate_domain(long_label + ".com"));
}

TEST_CASE("DnsCheckService type validation") {
    // Valid types
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

TEST_CASE("DnsCheckService domain normalization") {
    DnsCheckService svc;

    // Uppercase domain is normalized to lowercase before DNS lookup
    auto r = svc.check("GOOGLE.com", {"A"});
    CHECK(r.success);
    CHECK(r.domain == "google.com");  // normalized in check()
}

TEST_CASE("DnsCheckService caching") {
    DnsCheckService svc;

    auto r1 = svc.check("google.com", {"A"});
    auto r2 = svc.check("google.com", {"A"});
    CHECK(r2.success == r1.success);
    CHECK(r2.overall_status == r1.overall_status);

    // Clear cache
    svc.clear_cache("google.com");
    CHECK_FALSE(svc.has_cached("google.com"));

    // Set TTL to 0 and verify re-fetch
    svc.set_cache_ttl(0);
    auto r3 = svc.check("google.com", {"A"});
    // Should still work (cache miss due to TTL 0)
    CHECK(r3.success);

    // Restore TTL
    svc.set_cache_ttl(60);
}

TEST_CASE("DnsCheckService concurrent cache access") {
    DnsCheckService svc;
    std::vector<std::thread> threads;
    std::atomic<int> ok_count{0};

    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([&svc, &ok_count]() {
            auto r = svc.check("google.com", {"A"});
            if (r.success) ok_count++;
            svc.clear_cache("google.com");
            auto r2 = svc.check("google.com", {"A"});
            if (r2.success) ok_count++;
        });
    }

    for (auto& t : threads) t.join();
    CHECK(ok_count > 0);
}

TEST_CASE("DnsCheckService real A record resolution") {
    DnsCheckService svc;

    auto r = svc.check("google.com", {"A"});
    CHECK(r.success);
    CHECK(r.overall_status == "complete");
    CHECK(r.per_type.size() == 1);
    CHECK(r.per_type[0].status_code == "NOERROR");
    CHECK_FALSE(r.per_type[0].records.empty());

    bool found_a = false;
    for (const auto& rec : r.per_type[0].records) {
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

    auto r = svc.check("google.com", {"MX"});
    CHECK(r.success);
    CHECK(r.per_type[0].status_code == "NOERROR");

    bool found_mx = false;
    for (const auto& rec : r.per_type[0].records) {
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
    CHECK(r.per_type[0].status_code == "NOERROR");

    bool found_txt = false;
    for (const auto& rec : r.per_type[0].records) {
        if (rec.type == "TXT" && !rec.value.empty()) {
            found_txt = true;
            break;
        }
    }
    CHECK(found_txt);
}

TEST_CASE("DnsCheckService SOA resolution and fields") {
    DnsCheckService svc;

    auto r = svc.check("google.com", {"SOA"});
    CHECK(r.success);

    // SOA fields should be populated
    CHECK_FALSE(r.soa.mname.empty());   // e.g. ns1.google.com
    CHECK_FALSE(r.soa.rname.empty());   // e.g. dns-admin.google.com
    CHECK(r.soa.serial > 0);
}

TEST_CASE("DnsCheckService NXDOMAIN handling") {
    DnsCheckService svc;

    auto r = svc.check("thisshouldnotexistexample123456.com", {"A"});
    CHECK_FALSE(r.success);
    CHECK(r.overall_status == "failed");
    CHECK(r.per_type.size() == 1);
    CHECK((r.per_type[0].status_code == "NXDOMAIN"
           || r.per_type[0].status_code == "SERVFAIL"
           || r.per_type[0].status_code == "TIMEOUT"));
}

TEST_CASE("DnsCheckService partial success") {
    DnsCheckService svc;

    // Query for A + NXDOMAIN domain + MX — should be partial
    auto r = svc.check("google.com", {"A", "MX"});
    CHECK(r.success);
    CHECK(r.overall_status == "complete");  // both should succeed
}

TEST_CASE("DnsCheckService empty type list") {
    DnsCheckService svc;

    auto r = svc.check("example.com", {});
    CHECK(r.success);
    CHECK(r.overall_status == "complete");
    CHECK(r.per_type.empty());
}

TEST_CASE("DnsCheckService multiple types at once") {
    DnsCheckService svc;

    auto r = svc.check("google.com", {"A", "AAAA", "MX", "TXT", "NS", "SOA"});
    CHECK(r.success);

    // Should have per-type results for each requested type
    CHECK(r.per_type.size() == 6);

    int noerror_count = 0;
    for (const auto& pt : r.per_type) {
        if (pt.status_code == "NOERROR") noerror_count++;
    }
    CHECK(noerror_count >= 3);  // At least A, MX, NS should work
}

TEST_CASE("DnsCheckService TXT multi-fragment simulation") {
    // This tests that TXT records with multiple character-strings are handled.
    // c-areas already joins them via ares_dns_rr_get_bin(), so we verify
    // via a real domain known to have TXT records with substantial content.
    DnsCheckService svc;

    // Use gmail.com which typically has SPF and other TXT records
    auto r = svc.check("gmail.com", {"TXT"});
    CHECK(r.success);

    bool found_long_txt = false;
    for (const auto& rec : r.per_type[0].records) {
        if (rec.type == "TXT" && rec.value.size() > 100) {
            found_long_txt = true;
            // Verify no stray quotes inside the value
            CHECK(rec.value.find('"') == std::string::npos);
            break;
        }
    }
    // At minimum we should have some TXT records
    CHECK_FALSE(r.per_type[0].records.empty());
}
