#include "ssl/SslCertificateManager.h"

#include <cstdint>
#include <string>

#include "doctest/doctest.h"

TEST_CASE("SslCertificateManager create/find/list/remove") {
    containercp::ssl::SslCertificateManager mgr;

    uint64_t id = mgr.create(1, "example.com", "/certs/fullchain.pem", "/certs/privkey.pem");
    CHECK(id == 1);

    auto* c = mgr.find_by_domain("example.com");
    REQUIRE(c != nullptr);
    CHECK(c->domain == "example.com");
    CHECK(c->domain_id == 1);
    CHECK(c->status == "requested");
    CHECK(c->auto_renew);
    CHECK(c->enabled);

    CHECK(mgr.list().size() == 1);

    auto* by_id = mgr.find(id);
    REQUIRE(by_id != nullptr);
    CHECK(by_id->domain == "example.com");

    CHECK(mgr.remove(id));
    CHECK(mgr.find_by_domain("example.com") == nullptr);
}

TEST_CASE("SslCertificateManager find_expiring") {
    containercp::ssl::SslCertificateManager mgr;
    mgr.create(1, "active.com", "", "");
    mgr.create(2, "placeholder.com", "", "");

    auto* active = mgr.find_by_domain("active.com");
    if (active) active->status = "active";
    active = mgr.find_by_domain("placeholder.com");
    if (active) active->status = "placeholder";

    auto expiring = mgr.find_expiring();
    CHECK(expiring.size() == 1);
    CHECK(expiring[0]->domain == "active.com");
}

TEST_CASE("SslCertificateManager auto_renew flag") {
    containercp::ssl::SslCertificateManager mgr;
    mgr.create(1, "test.com", "", "");
    auto* c = mgr.find_by_domain("test.com");
    REQUIRE(c != nullptr);
    CHECK(c->auto_renew);

    c->auto_renew = false;
    CHECK_FALSE(c->auto_renew);
}
