#include "dns/DnsCheckService.h"
#include "doctest/doctest.h"

#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace containercp::dns;

// Helper: build a PerTypeResult with given status and count of dummy records
static PerTypeResult make_result(const std::string& type,
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
        rec.value = type + "_value_" + std::to_string(i);
        rec.ttl = 300;
        r.records.push_back(std::move(rec));
    }
    return r;
}

TEST_CASE("DnsCheckService domain format validation") {
    CHECK(DnsCheckService::validate_domain("example.com"));
    CHECK(DnsCheckService::validate_domain("sub.domain.co.uk"));
    CHECK(DnsCheckService::validate_domain("a-b.com"));
    CHECK(DnsCheckService::validate_domain("UPPERCASE.COM"));

    CHECK_FALSE(DnsCheckService::validate_domain("example.com; rm -rf /"));
    CHECK_FALSE(DnsCheckService::validate_domain("example.com|id"));
    CHECK_FALSE(DnsCheckService::validate_domain(""));
    CHECK_FALSE(DnsCheckService::validate_domain("192.168.1.1"));
    CHECK_FALSE(DnsCheckService::validate_domain("exa mple.com"));
    CHECK_FALSE(DnsCheckService::validate_domain(".example.com"));
    CHECK_FALSE(DnsCheckService::validate_domain("example.com."));

    std::string long_domain(300, 'a');
    CHECK_FALSE(DnsCheckService::validate_domain(long_domain));

    CHECK_FALSE(DnsCheckService::validate_domain("-bad.example.com"));
    CHECK_FALSE(DnsCheckService::validate_domain("bad-.example.com"));
    CHECK_FALSE(DnsCheckService::validate_domain("bad..example.com"));

    std::string long_label(65, 'a');
    CHECK_FALSE(DnsCheckService::validate_domain(long_label + ".com"));
}

TEST_CASE("DnsCheckService validate_dns_name — allows underscore for service records") {
    // validate_domain rejects these, validate_dns_name allows them
    CHECK(DnsCheckService::validate_dns_name("_dmarc.example.com"));
    CHECK(DnsCheckService::validate_dns_name("dkim._domainkey.example.com"));
    CHECK(DnsCheckService::validate_dns_name("_smtp._tls.example.com"));
    CHECK(DnsCheckService::validate_dns_name("_mta-sts.example.com"));
    CHECK(DnsCheckService::validate_dns_name("_autodiscover._tcp.example.com"));

    // Normal domains also work
    CHECK(DnsCheckService::validate_dns_name("example.com"));
    CHECK(DnsCheckService::validate_dns_name("sub.domain.co.uk"));

    // Still rejects invalid names
    CHECK_FALSE(DnsCheckService::validate_dns_name(""));
    CHECK_FALSE(DnsCheckService::validate_dns_name(".example.com"));
    CHECK_FALSE(DnsCheckService::validate_dns_name("bad..example.com"));
    CHECK_FALSE(DnsCheckService::validate_dns_name("bad name.com"));

    // Rejects shell chars
    CHECK_FALSE(DnsCheckService::validate_dns_name("example.com; id"));
    CHECK_FALSE(DnsCheckService::validate_dns_name("example.com|ls"));
    CHECK_FALSE(DnsCheckService::validate_dns_name("example.com/evil"));
    CHECK_FALSE(DnsCheckService::validate_dns_name("example.com?query"));

    // Rejects IP addresses (no alpha chars)
    CHECK_FALSE(DnsCheckService::validate_dns_name("192.168.1.1"));

    // Label too long
    std::string long_label(65, 'a');
    CHECK_FALSE(DnsCheckService::validate_dns_name(long_label + ".com"));

    // Overall too long
    std::string long_name(300, 'a');
    CHECK_FALSE(DnsCheckService::validate_dns_name(long_name));

    // Label starts/ends with hyphen (still allowed for underscore names)
    CHECK_FALSE(DnsCheckService::validate_dns_name("-bad.example.com"));
    CHECK_FALSE(DnsCheckService::validate_dns_name("bad-.example.com"));
}

TEST_CASE("DnsCheckService validate_domain still rejects underscore") {
    // Verify validate_domain still rejects underscore names
    CHECK_FALSE(DnsCheckService::validate_domain("_dmarc.example.com"));
    CHECK_FALSE(DnsCheckService::validate_domain("dkim._domainkey.example.com"));

    // Normal domains still pass
    CHECK(DnsCheckService::validate_domain("example.com"));
    CHECK(DnsCheckService::validate_domain("sub.domain.co.uk"));
}

TEST_CASE("DnsCheckService type validation") {
    CHECK(DnsCheckService::validate_type("A"));
    CHECK(DnsCheckService::validate_type("AAAA"));
    CHECK(DnsCheckService::validate_type("MX"));
    CHECK(DnsCheckService::validate_type("TXT"));
    CHECK(DnsCheckService::validate_type("CNAME"));
    CHECK(DnsCheckService::validate_type("NS"));
    CHECK(DnsCheckService::validate_type("SOA"));
    CHECK(DnsCheckService::validate_type("CAA"));

    CHECK_FALSE(DnsCheckService::validate_type("SPF"));
    CHECK_FALSE(DnsCheckService::validate_type("DMARC"));
    CHECK_FALSE(DnsCheckService::validate_type("INVALID"));
}

TEST_CASE("DnsCheckService overall_status: complete") {
    std::vector<PerTypeResult> pts = {
        make_result("A", "NOERROR", "", 2),
        make_result("MX", "NOERROR", "", 1),
    };
    bool success = false;
    std::string error;
    std::string status = DnsCheckService::compute_overall_status(pts, success, error);

    CHECK(status == "complete");
    CHECK(success);
    CHECK(error.empty());
}

TEST_CASE("DnsCheckService overall_status: partial") {
    std::vector<PerTypeResult> pts = {
        make_result("A", "NOERROR", "", 2),
        make_result("MX", "TIMEOUT", "TIMEOUT", 0),
    };
    bool success = false;
    std::string error;
    std::string status = DnsCheckService::compute_overall_status(pts, success, error);

    CHECK(status == "partial");
    CHECK(success);
    CHECK_FALSE(error.empty());
}

TEST_CASE("DnsCheckService overall_status: partial with NODATA") {
    std::vector<PerTypeResult> pts = {
        make_result("A", "NOERROR", "", 1),
        make_result("AAAA", "NODATA", "", 0),
        make_result("MX", "SERVFAIL", "SERVFAIL", 0),
    };
    bool success = false;
    std::string error;
    std::string status = DnsCheckService::compute_overall_status(pts, success, error);

    CHECK(status == "partial");
    CHECK(success);
    CHECK(error.find("SERVFAIL") != std::string::npos);
}

TEST_CASE("DnsCheckService overall_status: partial with single success") {
    std::vector<PerTypeResult> pts = {
        make_result("A", "NOERROR", "", 3),
        make_result("AAAA", "ERROR", "ERROR", 0),
        make_result("MX", "ERROR", "ERROR", 0),
    };
    bool success = false;
    std::string error;
    std::string status = DnsCheckService::compute_overall_status(pts, success, error);

    CHECK(status == "partial");
    CHECK(success);
}

TEST_CASE("DnsCheckService overall_status: failed (all resolver failures)") {
    std::vector<PerTypeResult> pts = {
        make_result("A", "TIMEOUT", "TIMEOUT"),
        make_result("AAAA", "SERVFAIL", "SERVFAIL"),
    };
    bool success = true;
    std::string error;
    std::string status = DnsCheckService::compute_overall_status(pts, success, error);

    CHECK(status == "failed");
    CHECK_FALSE(success);
    CHECK_FALSE(error.empty());
}

TEST_CASE("DnsCheckService overall_status: NXDOMAIN is complete") {
    // NXDOMAIN is a valid DNS response — should be "complete" not "failed"
    std::vector<PerTypeResult> pts = {
        make_result("A", "NXDOMAIN", "NXDOMAIN"),
    };
    bool success = false;
    std::string error;
    std::string status = DnsCheckService::compute_overall_status(pts, success, error);

    CHECK(status == "complete");
    CHECK(success);
    CHECK(error.empty());
}

TEST_CASE("DnsCheckService overall_status: NODATA is complete") {
    std::vector<PerTypeResult> pts = {
        make_result("AAAA", "NODATA", ""),
    };
    bool success = false;
    std::string error;
    std::string status = DnsCheckService::compute_overall_status(pts, success, error);

    CHECK(status == "complete");
    CHECK(success);
}

TEST_CASE("DnsCheckService overall_status: mixed NXDOMAIN + NOERROR = complete") {
    std::vector<PerTypeResult> pts = {
        make_result("A", "NOERROR", "", 2),
        make_result("AAAA", "NXDOMAIN", "NXDOMAIN"),
        make_result("MX", "NODATA", ""),
    };
    bool success = false;
    std::string error;
    std::string status = DnsCheckService::compute_overall_status(pts, success, error);

    CHECK(status == "complete");
    CHECK(success);
}

TEST_CASE("DnsCheckService overall_status: mixed NXDOMAIN + TIMEOUT = partial") {
    std::vector<PerTypeResult> pts = {
        make_result("A", "NXDOMAIN", "NXDOMAIN"),
        make_result("AAAA", "TIMEOUT", "TIMEOUT"),
    };
    bool success = false;
    std::string error;
    std::string status = DnsCheckService::compute_overall_status(pts, success, error);

    CHECK(status == "partial");
    CHECK(success);  // partial = still successful overall
    CHECK_FALSE(error.empty());
}

TEST_CASE("DnsCheckService HTTP status: NXDOMAIN → 200") {
    std::vector<PerTypeResult> pts = {
        make_result("A", "NXDOMAIN", "NXDOMAIN"),
    };
    int status = DnsCheckService::compute_http_status(pts, true);
    CHECK(status == 200);
}

TEST_CASE("DnsCheckService HTTP status: NOERROR → 200") {
    std::vector<PerTypeResult> pts = {
        make_result("A", "NOERROR", "", 1),
    };
    int status = DnsCheckService::compute_http_status(pts, true);
    CHECK(status == 200);
}

TEST_CASE("DnsCheckService HTTP status: partial (NOERROR + TIMEOUT) → 200") {
    std::vector<PerTypeResult> pts = {
        make_result("A", "NOERROR", "", 2),
        make_result("AAAA", "TIMEOUT", "TIMEOUT"),
    };
    int status = DnsCheckService::compute_http_status(pts, true);
    CHECK(status == 200);
}

TEST_CASE("DnsCheckService HTTP status: all SERVFAIL → 502") {
    std::vector<PerTypeResult> pts = {
        make_result("A", "SERVFAIL", "SERVFAIL"),
    };
    int status = DnsCheckService::compute_http_status(pts, false);
    CHECK(status == 502);
}

TEST_CASE("DnsCheckService HTTP status: mixed NXDOMAIN + SERVFAIL → 200") {
    std::vector<PerTypeResult> pts = {
        make_result("A", "NXDOMAIN", "NXDOMAIN"),
        make_result("AAAA", "SERVFAIL", "SERVFAIL"),
    };
    int status = DnsCheckService::compute_http_status(pts, true);  // partial
    CHECK(status == 200);
}

TEST_CASE("DnsCheckService overall_status: empty list") {
    std::vector<PerTypeResult> pts;
    bool success = false;
    std::string error;
    std::string status = DnsCheckService::compute_overall_status(pts, success, error);

    CHECK(status == "complete");
    CHECK(success);
    CHECK(error.empty());
}

TEST_CASE("DnsCheckService empty type list") {
    DnsCheckService svc;

    auto r = svc.check("example.com", {});
    CHECK(r.success);
    CHECK(r.overall_status == "complete");
    CHECK(r.per_type.empty());
}

TEST_SUITE("[integration]") {

TEST_CASE("DnsCheckService domain normalization — requires live DNS") {
    DnsCheckService svc;

    auto r = svc.check("GOOGLE.com", {"A"});
    CHECK(r.success);
    CHECK(r.domain == "google.com");
}

TEST_CASE("DnsCheckService caching — requires live DNS") {
    DnsCheckService svc;

    auto r1 = svc.check("google.com", {"A"});
    auto r2 = svc.check("google.com", {"A"});
    CHECK(r2.success == r1.success);
    CHECK(r2.overall_status == r1.overall_status);

    svc.clear_cache("google.com");
    CHECK_FALSE(svc.has_cached("google.com"));

    svc.set_cache_ttl(0);
    auto r3 = svc.check("google.com", {"A"});
    CHECK(r3.success);

    svc.set_cache_ttl(60);
}

TEST_CASE("DnsCheckService concurrent cache access — requires live DNS") {
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

TEST_CASE("DnsCheckService real A record — requires live DNS") {
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

TEST_CASE("DnsCheckService MX resolution — requires live DNS") {
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

TEST_CASE("DnsCheckService TXT resolution — requires live DNS") {
    DnsCheckService svc;

    auto r = svc.check("google.com", {"TXT"});
    CHECK(r.success);
    CHECK(r.per_type[0].status_code == "NOERROR");

    bool found_txt = false;
    for (const auto& rec : r.per_type[0].records) {
        if (rec.type == "TXT" && !rec.value.empty()) {
            found_txt = true;
            CHECK(rec.value.find('"') == std::string::npos);
            break;
        }
    }
    CHECK(found_txt);
}

TEST_CASE("DnsCheckService SOA fields — requires live DNS") {
    DnsCheckService svc;

    auto r = svc.check("google.com", {"SOA"});
    CHECK(r.success);

    CHECK_FALSE(r.soa.mname.empty());
    CHECK_FALSE(r.soa.rname.empty());
    CHECK(r.soa.serial > 0);
}

TEST_CASE("DnsCheckService NXDOMAIN handling — requires live DNS") {
    DnsCheckService svc;

    auto r = svc.check("thisshouldnotexistexample123456.com", {"A"});
    CHECK(r.per_type.size() == 1);
    std::string sc = r.per_type[0].status_code;
    bool is_nx = sc == "NXDOMAIN";
    bool is_fail = sc == "SERVFAIL" || sc == "TIMEOUT";
    bool ok = is_nx || is_fail;
    CHECK(ok);
    if (is_nx) {
        CHECK(r.success);
        CHECK(r.overall_status == "complete");
    } else if (is_fail) {
        CHECK_FALSE(r.success);
        CHECK(r.overall_status == "failed");
    }
}

TEST_CASE("DnsCheckService multiple types — requires live DNS") {
    DnsCheckService svc;

    auto r = svc.check("google.com", {"A", "AAAA", "MX", "TXT", "NS", "SOA"});
    CHECK(r.success);
    CHECK(r.per_type.size() == 6);

    int noerror_count = 0;
    for (const auto& pt : r.per_type) {
        if (pt.status_code == "NOERROR") noerror_count++;
    }
    CHECK(noerror_count >= 3);
}

TEST_CASE("DnsCheckService AAAA (IPv6) record — requires live DNS") {
    DnsCheckService svc;

    auto r = svc.check("google.com", {"AAAA"});
    CHECK(r.success);
    CHECK(r.per_type.size() == 1);
    CHECK(r.per_type[0].status_code == "NOERROR");
    CHECK_FALSE(r.per_type[0].records.empty());

    bool found_aaaa = false;
    for (const auto& rec : r.per_type[0].records) {
        if (rec.type == "AAAA" && !rec.value.empty()) {
            found_aaaa = true;
            CHECK(rec.value.find(':') != std::string::npos);
            CHECK(rec.ttl > 0);
            CHECK_FALSE(rec.dns_response_details.empty());
        }
    }
    CHECK(found_aaaa);
}

TEST_CASE("DnsCheckService cache TTL expiry — sleeps 2s, requires live DNS") {
    DnsCheckService svc;

    svc.set_cache_ttl(1);
    auto r1 = svc.check("google.com", {"A"});
    CHECK_FALSE(r1.cached);

    auto r2 = svc.check("google.com", {"A"});
    CHECK(r2.cached);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto r3 = svc.check("google.com", {"A"});
    CHECK_FALSE(r3.cached);

    svc.set_cache_ttl(60);
}

} // TEST_SUITE("[integration]")
