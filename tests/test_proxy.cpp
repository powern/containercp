#include "proxy/ProxyConfigBuilder.h"
#include "proxy/ReverseProxyManager.h"

#include <cstdint>
#include <string>

#include "doctest/doctest.h"

TEST_CASE("ReverseProxyManager create/find/list/remove") {
    containercp::proxy::ReverseProxyManager mgr;

    uint64_t id = mgr.create("example.com", 1, "/path/config", "http://127.0.0.1:80");
    CHECK(id == 1);

    auto* p = mgr.find_by_domain("example.com");
    REQUIRE(p != nullptr);
    CHECK(p->domain == "example.com");
    CHECK(p->site_id == 1);
    CHECK(p->provider == "nginx");
    CHECK(p->upstream == "http://127.0.0.1:80");
    CHECK(p->enabled);
    CHECK(p->status == "active");

    CHECK(mgr.list().size() == 1);

    CHECK(mgr.remove(id));
    CHECK(mgr.find_by_domain("example.com") == nullptr);
}

TEST_CASE("ProxyConfigBuilder normalize_upstream") {
    using containercp::proxy::ProxyConfigBuilder;

    CHECK(ProxyConfigBuilder::normalize_upstream("site-4-web:80") == "site-4-web:80");
    CHECK(ProxyConfigBuilder::normalize_upstream("/site-4-web:80") == "site-4-web:80");
    CHECK(ProxyConfigBuilder::normalize_upstream("http://site-4-web:80") == "site-4-web:80");
    CHECK(ProxyConfigBuilder::normalize_upstream("http:///site-4-web:80") == "site-4-web:80");
    CHECK(ProxyConfigBuilder::normalize_upstream("ttp:///site-4-web:80") == "site-4-web:80");
    CHECK(ProxyConfigBuilder::normalize_upstream("site-0-web:80;") == "site-0-web:80");
    CHECK(ProxyConfigBuilder::normalize_upstream("") == "");
}

TEST_CASE("ReverseProxyManager multiple") {
    containercp::proxy::ReverseProxyManager mgr;
    mgr.create("a.com", 1, "", "");
    mgr.create("b.com", 2, "", "");
    CHECK(mgr.list().size() == 2);
    CHECK(mgr.find_by_domain("a.com") != nullptr);
    CHECK(mgr.find_by_domain("b.com") != nullptr);
    CHECK(mgr.find_by_domain("c.com") == nullptr);
}
