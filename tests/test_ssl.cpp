#include "ssl/SslCertificateManager.h"

#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"

TEST_CASE("SslCertificateManager create/find/list/remove") {
    containercp::ssl::SslCertificateManager mgr;

    uint64_t id = mgr.create(1, "example.com", "/certs/fullchain.pem", "/certs/privkey.pem");
    CHECK(id == 1);

    auto* c = mgr.find_by_domain("example.com");
    REQUIRE(c != nullptr);
    CHECK(c->domain == "example.com");
    CHECK(c->domain_id == 1);
    CHECK(c->status == "http_only");
    CHECK(c->auto_renew);
    CHECK_FALSE(c->https_enabled);
    CHECK_FALSE(c->redirect_enabled);
    CHECK(c->version == 1);

    CHECK(mgr.list().size() == 1);

    auto* by_id = mgr.find(id);
    REQUIRE(by_id != nullptr);
    CHECK(by_id->domain == "example.com");

    CHECK(mgr.remove(id));
    CHECK(mgr.find_by_domain("example.com") == nullptr);
}

TEST_CASE("SslCertificateManager extended methods") {
    containercp::ssl::SslCertificateManager mgr;
    uint64_t id = mgr.create(1, "test.com", "", "");

    mgr.update_status(id, "active");
    CHECK(mgr.find(id)->status == "active");

    mgr.update_https(id, true, true);
    CHECK(mgr.find(id)->https_enabled);
    CHECK(mgr.find(id)->redirect_enabled);

    mgr.set_metadata(id, "2025-07-08", "2025-10-06", "2025-09-06");
    CHECK(mgr.find(id)->issued_at == "2025-07-08");
    CHECK(mgr.find(id)->expires_at == "2025-10-06");
    CHECK(mgr.find(id)->renew_after == "2025-09-06");

    mgr.set_domains(id, "test.com,www.test.com");
    CHECK(mgr.find(id)->domains == "test.com,www.test.com");

    mgr.set_error(id, "ACME failed");
    CHECK(mgr.find(id)->status == "error");
    CHECK(mgr.find(id)->last_error == "ACME failed");
}

TEST_CASE("SslCertificateManager find_by_status") {
    containercp::ssl::SslCertificateManager mgr;
    mgr.create(1, "active.com", "", "");
    mgr.create(2, "error.com", "", "");
    mgr.create(3, "disabled.com", "", "");

    mgr.update_status(1, "active");
    mgr.update_status(2, "error");
    mgr.update_status(3, "disabled");

    auto active = mgr.find_by_status("active");
    CHECK(active.size() == 1);
    CHECK(active[0]->domain == "active.com");

    auto errors = mgr.find_by_status("error");
    CHECK(errors.size() == 1);
    CHECK(errors[0]->domain == "error.com");
}

TEST_CASE("SslCertificateManager find_due_for_renewal") {
    containercp::ssl::SslCertificateManager mgr;
    mgr.create(1, "renewable.com", "", "");
    mgr.create(2, "notrenewable.com", "", "");

    mgr.update_status(1, "active");
    mgr.update_status(2, "active");

    auto* c = mgr.find_by_domain("renewable.com");
    REQUIRE(c != nullptr);
    c->auto_renew = true;
    c->renew_after = "2025-01-01";

    c = mgr.find_by_domain("notrenewable.com");
    REQUIRE(c != nullptr);
    c->auto_renew = false;

    auto due = mgr.find_due_for_renewal();
    CHECK(due.size() == 1);
    CHECK(due[0]->domain == "renewable.com");
}

TEST_CASE("SslCertificateManager set_certificates with backward compat") {
    containercp::ssl::SslCertificateManager mgr;

    // Simulate loading old-format certs (missing new fields)
    containercp::ssl::SslCertificate old_cert;
    old_cert.id = 1;
    old_cert.name = "old.com";
    old_cert.domain = "old.com";
    old_cert.domain_id = 1;
    old_cert.provider = "placeholder";
    old_cert.status = "placeholder";
    old_cert.auto_renew = true;

    std::vector<containercp::ssl::SslCertificate> certs = {old_cert};
    mgr.set_certificates(certs);

    CHECK(mgr.list().size() == 1);
    auto* c = mgr.find_by_domain("old.com");
    REQUIRE(c != nullptr);
    CHECK(c->status == "placeholder");
    CHECK(c->last_error.empty());
    CHECK(c->renew_attempts == 0);
    CHECK(c->version == 1);
}
