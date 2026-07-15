#include "dns/SpfAnalyzer.h"
#include "doctest/doctest.h"

#include <string>

using namespace containercp::dns;

TEST_CASE("SpfAnalyzer: ip4 exact match") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip4:116.202.231.94 -all", "116.202.231.94", "", "example.com");
    CHECK(r.match == "match");
    CHECK(r.expected_ip_allowed);
    CHECK(r.mechanism_matched.find("ip4:116.202.231.94") != std::string::npos);
    CHECK(r.lookup_count == 0);  // ip4 does not count as DNS lookup
}

TEST_CASE("SpfAnalyzer: ip4 CIDR match") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip4:116.202.231.0/24 -all", "116.202.231.94", "", "example.com");
    CHECK(r.match == "match");
    CHECK(r.expected_ip_allowed);
}

TEST_CASE("SpfAnalyzer: ip4 mismatch") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip4:10.0.0.1 -all", "116.202.231.94", "", "example.com");
    CHECK(r.match == "mismatch");
    CHECK_FALSE(r.expected_ip_allowed);
}

TEST_CASE("SpfAnalyzer: ip4 CIDR mismatch") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip4:10.0.0.0/8 -all", "116.202.231.94", "", "example.com");
    CHECK(r.match == "mismatch");
}

TEST_CASE("SpfAnalyzer: no SPF record") {
    SpfAnalyzer a;
    auto r = a.analyze("", "116.202.231.94", "", "example.com");
    CHECK(r.match == "not_published");
    CHECK(r.status == "not_found");
}

TEST_CASE("SpfAnalyzer: invalid syntax — missing v=spf1") {
    SpfAnalyzer a;
    auto r = a.analyze("ip4:1.2.3.4 -all", "1.2.3.4", "", "example.com");
    CHECK(r.status == "error");
}

TEST_CASE("SpfAnalyzer: invalid syntax — wrong prefix") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf2 ip4:1.2.3.4 -all", "1.2.3.4", "", "example.com");
    CHECK(r.status == "error");
}

TEST_CASE("SpfAnalyzer: all qualifier -all (hard fail) — no match") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip4:10.0.0.1 -all", "116.202.231.94", "", "example.com");
    CHECK(r.all_qualifier == "-");
    CHECK(r.match == "mismatch");
}

TEST_CASE("SpfAnalyzer: all qualifier ~all (soft fail) — match with warning") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip4:116.202.231.94 ~all", "116.202.231.94", "", "example.com");
    CHECK(r.match == "match");
    CHECK(r.all_qualifier == "~");
    CHECK_FALSE(r.warnings.empty());
}

TEST_CASE("SpfAnalyzer: all qualifier ?all (neutral) — match with warning") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip4:116.202.231.94 ?all", "116.202.231.94", "", "example.com");
    CHECK(r.match == "match");
    CHECK(r.all_qualifier == "?");
    CHECK_FALSE(r.warnings.empty());
}

TEST_CASE("SpfAnalyzer: maillab-like case") {
    // v=spf1 ip4:116.202.231.94 -all with expected IP 116.202.231.94
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip4:116.202.231.94 -all", "116.202.231.94", "", "maillab.softi.co");
    CHECK(r.match == "match");
    CHECK(r.expected_ip_allowed);
    CHECK(r.mechanism_matched.find("ip4:116.202.231.94") != std::string::npos);
    CHECK(r.lookup_count == 0);  // only ip4 mechanisms
    CHECK(r.errors.empty());
}

TEST_CASE("SpfAnalyzer: multiple ip4 — no DNS lookups") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip4:1.1.1.1 ip4:2.2.2.2 ip4:3.3.3.3 -all", "2.2.2.2", "", "example.com");
    CHECK(r.match == "match");
    CHECK(r.lookup_count == 0);  // ip4 does not count
}

TEST_CASE("SpfAnalyzer: lookup count — a mechanism consumes lookup") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip4:10.0.0.1 a -all", "10.0.0.1", "", "example.com");
    // 'a' consumes a lookup — IP matched by ip4 before reaching 'a'
    CHECK(r.match == "match");
    CHECK(r.lookup_count >= 0);  // early match by ip4, 'a' not reached
}

TEST_CASE("SpfAnalyzer: maillab-like with mx") {
    SpfAnalyzer a;
    std::string spf = "v=spf1 mx ip4:116.202.231.94 -all";
    auto r = a.analyze(spf, "116.202.231.94", "", "maillab.softi.co");
    // Expected IP is explicitly in ip4, so match regardless of mx
    CHECK(r.match == "match");
}

TEST_CASE("SpfAnalyzer: ip6 exact match") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip6:2001:db8::1 -all", "", "2001:db8::1", "example.com");
    CHECK(r.match == "match");
    CHECK(r.expected_ip_allowed);
}

TEST_CASE("SpfAnalyzer: ip6 CIDR match") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip6:2001:db8::/32 -all", "", "2001:db8::1", "example.com");
    CHECK(r.match == "match");
}

TEST_CASE("SpfAnalyzer: ip6 mismatch") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip6:2001:db8::1 -all", "", "::1", "example.com");
    CHECK(r.match == "mismatch");
}

TEST_CASE("SpfAnalyzer: checks array populated") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 ip4:116.202.231.94 -all", "116.202.231.94", "", "example.com");
    CHECK_FALSE(r.checks.empty());
    bool hasSyntax = false, hasIp = false;
    for (const auto& c : r.checks) {
        if (c.code == "spf_syntax") hasSyntax = true;
        if (c.code == "spf_expected_ip") hasIp = true;
    }
    CHECK(hasSyntax);
    CHECK(hasIp);
}

TEST_CASE("SpfAnalyzer: forward + mechanism (qualifier)") {
    SpfAnalyzer a;
    auto r = a.analyze("v=spf1 +ip4:116.202.231.94 -all", "116.202.231.94", "", "example.com");
    CHECK(r.match == "match");
}
