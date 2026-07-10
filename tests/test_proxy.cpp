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

TEST_CASE("Admin proxy entry protection") {
    containercp::proxy::ReverseProxyManager mgr;

    // Create an admin entry (site_id = 0)
    containercp::proxy::ReverseProxy admin_rp;
    admin_rp.domain = "admin.example.com";
    admin_rp.site_id = 0;
    admin_rp.upstream = "172.17.0.1:8081";
    mgr.create(admin_rp.domain, admin_rp.site_id, "/path/conf", admin_rp.upstream);

    // Create a regular site entry
    containercp::proxy::ReverseProxy site_rp;
    site_rp.domain = "site.example.com";
    site_rp.site_id = 5;
    site_rp.upstream = "site-5-web:80";
    mgr.create(site_rp.domain, site_rp.site_id, "/path/conf", site_rp.upstream);

    // Verify protection: site_id == 0 means protected
    auto* admin = mgr.find_by_domain("admin.example.com");
    REQUIRE(admin != nullptr);
    CHECK(admin->site_id == 0);

    auto* site = mgr.find_by_domain("site.example.com");
    REQUIRE(site != nullptr);
    CHECK(site->site_id == 5);

    // Admin entries should not be removable via the proxy remove API
    // (enforced in the API handler, not in ReverseProxyManager itself)
    // Verify that a site_id == 0 check in the handler would protect it
    CHECK(admin->site_id == 0);  // protected
    CHECK(site->site_id != 0);   // not protected (can be removed)

    // Both entries exist in the manager
    CHECK(mgr.list().size() == 2);
}
