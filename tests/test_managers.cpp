#include "user/UserManager.h"
#include "site/SiteManager.h"

#include <cstdint>
#include <string>

#include "doctest/doctest.h"

TEST_CASE("UserManager create/find/list/remove") {
    containercp::user::UserManager mgr;

    uint64_t id = mgr.create("testuser", 1001, "/home/testuser", "/bin/bash");
    CHECK(id == 1);

    auto* u = mgr.find("testuser");
    REQUIRE(u != nullptr);
    CHECK(u->username == "testuser");
    CHECK(u->uid == 1001);
    CHECK(u->home_directory == "/home/testuser");
    CHECK(u->shell == "/bin/bash");
    CHECK(u->enabled);

    CHECK(mgr.list().size() == 1);

    CHECK(mgr.remove(id));
    CHECK(mgr.find("testuser") == nullptr);
    CHECK(mgr.list().empty());

    CHECK_FALSE(mgr.remove(999));
}

TEST_CASE("UserManager duplicate name") {
    containercp::user::UserManager mgr;
    mgr.create("dup", 1001, "/home/dup", "/bin/bash");
    mgr.create("dup", 1002, "/home/dup2", "/bin/bash");
    CHECK(mgr.list().size() == 2);
    auto* u = mgr.find("dup");
    REQUIRE(u != nullptr);
}

TEST_CASE("SiteManager create/find/list/remove") {
    containercp::site::SiteManager mgr;

    uint64_t id = mgr.create("example.com", "admin", 1);
    CHECK(id == 1);

    auto* s = mgr.find("example.com");
    REQUIRE(s != nullptr);
    CHECK(s->domain == "example.com");
    CHECK(s->owner == "admin");
    CHECK(s->node_id == 1);
    CHECK(s->id == 1);

    CHECK(mgr.list().size() == 1);

    CHECK(mgr.remove(id));
    CHECK(mgr.find("example.com") == nullptr);
    CHECK(mgr.list().empty());
}

TEST_CASE("SiteManager find_by_id") {
    containercp::site::SiteManager mgr;
    uint64_t id1 = mgr.create("site1.com", "admin", 1);
    uint64_t id2 = mgr.create("site2.com", "admin", 1);

    auto* s = mgr.find_by_id(id1);
    REQUIRE(s != nullptr);
    CHECK(s->domain == "site1.com");
    CHECK(s->id == id1);

    s = mgr.find_by_id(id2);
    REQUIRE(s != nullptr);
    CHECK(s->domain == "site2.com");
    CHECK(s->id == id2);

    CHECK(mgr.find_by_id(999) == nullptr);
}

TEST_CASE("SiteManager remove cleans state") {
    containercp::site::SiteManager mgr;
    mgr.create("test.com", "admin", 1);
    mgr.create("other.com", "admin", 1);
    CHECK(mgr.list().size() == 2);

    // Remove one site
    auto* s = mgr.find("test.com");
    REQUIRE(s != nullptr);
    mgr.remove(s->id);

    // Only the other site remains
    CHECK(mgr.list().size() == 1);
    CHECK(mgr.find("test.com") == nullptr);
    CHECK(mgr.find("other.com") != nullptr);
}

#include "domain/DomainManager.h"
#include "domain/DomainViewService.h"
#include "ssl/CertificateStore.h"
#include "logger/Logger.h"

#include <cstdio>
#include <fstream>
#include <sys/stat.h>

TEST_CASE("DomainViewService produces valid JSON for various target values") {
    using namespace containercp;

    // Setup: temporary SSL root so CertificateStore can be created
    char tmp[] = "/tmp/containercp_test_domain_json_XXXXXX";
    char* ssl_root = mkdtemp(tmp);
    REQUIRE(ssl_root != nullptr);
    std::string ssl_root_str(ssl_root);

    // Create managers with known test data
    domain::DomainManager domains;
    site::SiteManager sites;
    ssl::CertificateStore cert_store(logger::Logger::instance(), ssl_root_str);

    // Add a site referenced by domains
    sites.create("testsite.com", "admin", 1);

    // Create domains with various target scenarios
    uint64_t d1 = domains.create("empty-target.com", 1, 1, "primary", "");
    uint64_t d2 = domains.create("normal-target.com", 1, 1, "alias", "testsite.com");
    uint64_t d3 = domains.create("redirect-target.com", 1, 1, "redirect", "https://example.com/page");
    uint64_t d4 = domains.create("unlinked.com", 1, 999, "primary", "");  // site_id 999 doesn't exist
    (void)d1; (void)d2; (void)d3; (void)d4;

    // Write SSL metadata for site_id=1 so cert_store.load_metadata works
    {
        std::string dir = ssl_root_str + "/1/current";
        ::mkdir((ssl_root_str + "/1").c_str(), 0755);
        ::mkdir(dir.c_str(), 0755);
        std::ofstream f(dir + "/metadata.json");
        f << R"({"site_id":1,"status":"active","https_enabled":true,"expires_at":"2030-01-01T00:00:00Z"})";
        f.close();
    }

    // Create the view service and generate enriched JSON
    domain::DomainViewService view(logger::Logger::instance(), domains, sites, cert_store);
    std::string json_result = view.build_enriched_json();

    // Verify the JSON is syntactically valid by checking basic structure
    CHECK(json_result.size() > 0);
    CHECK(json_result.front() == '[');
    CHECK(json_result.back() == ']');

    // Verify each domain's JSON contains the expected patterns
    CHECK(json_result.find("\"domain\":\"empty-target.com\"") != std::string::npos);
    CHECK(json_result.find("\"domain\":\"normal-target.com\"") != std::string::npos);
    CHECK(json_result.find("\"domain\":\"redirect-target.com\"") != std::string::npos);
    CHECK(json_result.find("\"domain\":\"unlinked.com\"") != std::string::npos);

    // Verify target fields are correctly formatted
    CHECK(json_result.find("\"target\":\"\",\"ssl_enabled\"") != std::string::npos);
    CHECK(json_result.find("\"target\":\"testsite.com\",\"ssl_enabled\"") != std::string::npos);
    CHECK(json_result.find("\"target\":\"https://example.com/page\",\"ssl_enabled\"") != std::string::npos);

    // Verify SSL status for the linked site
    CHECK(json_result.find("\"ssl_status\":\"Active\"") != std::string::npos);

    // Verify unlinked site has no site name
    CHECK(json_result.find("\"site_name\":\"\",\"site_domain\":\"\"") != std::string::npos);

    // Cleanup
    std::string rm_cmd = "rm -rf " + ssl_root_str;
    std::system(rm_cmd.c_str());
}
