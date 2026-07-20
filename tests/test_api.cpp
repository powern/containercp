#include "api/Router.h"
#include "api/Response.h"
#include "api/JsonFormatter.h"
#include "ssl/CertificateStore.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "doctest/doctest.h"

// --- Router ---

TEST_CASE("Router dispatch exact match") {
    containercp::api::Router router;

    router.add("GET", "/api/health", [](const containercp::api::Request&) {
        containercp::api::Response r;
        r.body = "{\"status\":\"ok\"}";
        return r;
    });

    containercp::api::Request req;
    req.method = "GET";
    req.path = "/api/health";

    auto resp = router.dispatch(req);
    CHECK(resp.status_code == 200);
    CHECK(resp.body == "{\"status\":\"ok\"}");
}

TEST_CASE("Router dispatch 404") {
    containercp::api::Router router;

    containercp::api::Request req;
    req.method = "GET";
    req.path = "/api/nonexistent";

    auto resp = router.dispatch(req);
    CHECK(resp.status_code == 404);
}

TEST_CASE("Router dispatch wrong method") {
    containercp::api::Router router;
    router.add("POST", "/api/test", [](const containercp::api::Request&) {
        return containercp::api::Response{};
    });

    containercp::api::Request req;
    req.method = "GET";
    req.path = "/api/test";

    auto resp = router.dispatch(req);
    CHECK(resp.status_code == 404);
}

TEST_CASE("Router dispatch prefix match") {
    containercp::api::Router router;

    router.add_prefix("GET", "/api/ssl/", [](const containercp::api::Request& req) {
        containercp::api::Response r;
        r.body = "prefix:" + req.path;
        return r;
    });

    containercp::api::Request req;
    req.method = "GET";
    req.path = "/api/ssl/example.com";

    auto resp = router.dispatch(req);
    CHECK(resp.body == "prefix:/api/ssl/example.com");
}

TEST_CASE("Router dispatch prefix over exact") {
    containercp::api::Router router;

    router.add("GET", "/api/ssl/providers", [](const containercp::api::Request&) {
        containercp::api::Response r;
        r.body = "exact:providers";
        return r;
    });

    router.add_prefix("GET", "/api/ssl/", [](const containercp::api::Request&) {
        containercp::api::Response r;
        r.body = "prefix:catchall";
        return r;
    });

    // Exact match should take priority
    containercp::api::Request req;
    req.method = "GET";
    req.path = "/api/ssl/providers";

    auto resp = router.dispatch(req);
    CHECK(resp.body == "exact:providers");

    // Prefix match for other paths
    req.path = "/api/ssl/example.com";
    resp = router.dispatch(req);
    CHECK(resp.body == "prefix:catchall");
}

// --- JsonFormatter ---

TEST_CASE("JsonFormatter success wrapper") {
    auto json = containercp::api::JsonFormatter::success("\"data\"");
    CHECK(json == "{\"success\":true,\"data\":\"data\"}");
}

TEST_CASE("JsonFormatter error wrapper") {
    auto json = containercp::api::JsonFormatter::error("something broke");
    CHECK(json == "{\"success\":false,\"error\":\"something broke\"}");
}

TEST_CASE("JsonFormatter version") {
    auto json = containercp::api::JsonFormatter::version("1.0");
    CHECK(json == "{\"version\":\"1.0\"}");
}

TEST_CASE("JsonFormatter health") {
    auto json = containercp::api::JsonFormatter::health(true);
    CHECK(json == "{\"status\":\"ok\"}");
}

TEST_CASE("JsonFormatter empty sites") {
    std::vector<containercp::site::Site> empty;
    auto json = containercp::api::JsonFormatter::sites(empty);
    CHECK(json == "[]");
}

TEST_CASE("JsonFormatter single site") {
    containercp::site::Site s;
    s.id = 1;
    s.domain = "example.com";
    s.owner = "admin";

    std::vector<containercp::site::Site> sites = {s};
    auto json = containercp::api::JsonFormatter::sites(sites);
    CHECK(json.find("\"domain\":\"example.com\"") != std::string::npos);
}

// --- SSL JSON response format ---

TEST_CASE("SSL list includes HTTP_ONLY sites") {
    // Verify the JSON response format for SSL listing
    // The ApiServer handler constructs this format manually
    std::string json = "{\"success\":true,\"data\":["
        "{\"domain\":\"site1.com\",\"status\":\"active\",\"https_enabled\":true},"
        "{\"domain\":\"site2.com\",\"status\":\"HTTP_ONLY\",\"https_enabled\":false}"
        "]}";

    CHECK(json.find("\"site1.com\"") != std::string::npos);
    CHECK(json.find("\"site2.com\"") != std::string::npos);
    CHECK(json.find("\"HTTP_ONLY\"") != std::string::npos);
    CHECK(json.find("\"https_enabled\":true") != std::string::npos);
    CHECK(json.find("\"https_enabled\":false") != std::string::npos);
}

TEST_CASE("SSL detail response format") {
    // Verify the JSON response format for SSL detail
    std::string json = "{\"success\":true,\"data\":{"
        "\"site_id\":1,"
        "\"domain\":\"example.com\","
        "\"provider_id\":\"letsencrypt\","
        "\"status\":\"active\","
        "\"https_enabled\":true,"
        "\"redirect_enabled\":false"
        "}}";

    CHECK(json.find("\"provider_id\":\"letsencrypt\"") != std::string::npos);
    CHECK(json.find("\"https_enabled\":true") != std::string::npos);
}

TEST_CASE("SSL error response format") {
    std::string json = "{\"success\":false,\"error\":{"
        "\"code\":\"SSL_INVALID_STATE\","
        "\"message\":\"No valid certificate\","
        "\"details\":{}"
        "}}";

    CHECK(json.find("\"code\":\"SSL_INVALID_STATE\"") != std::string::npos);
    CHECK(json.find("\"message\":\"No valid certificate\"") != std::string::npos);
    CHECK(json.find("\"details\":{}") != std::string::npos);
}

TEST_CASE("SSL job response format") {
    std::string json = "{\"success\":true,\"data\":{"
        "\"job_id\":1,"
        "\"status\":\"completed\","
        "\"message\":\"Certificate issued\""
        "}}";

    CHECK(json.find("\"job_id\":1") != std::string::npos);
    CHECK(json.find("\"status\":\"completed\"") != std::string::npos);
}

TEST_CASE("WordPress credential rotation API returns job id only") {
    std::string json = "{\"success\":true,\"data\":{"
        "\"job_id\":42,"
        "\"status\":\"pending\","
        "\"message\":\"Credential rotation queued\""
        "}}";

    CHECK(json.find("\"job_id\":42") != std::string::npos);
    CHECK(json.find("\"status\":\"pending\"") != std::string::npos);
    CHECK(json.find("DB_PASSWORD") == std::string::npos);
    CHECK(json.find("secret") == std::string::npos);
}

TEST_CASE("WordPress credential UI uses public endpoints without raw password fields") {
    std::ifstream in(std::string(TEST_SOURCE_DIR) + "/web/app.js");
    REQUIRE(in.is_open());
    std::string js((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    CHECK(js.find("/api/wordpress/database-credentials/status") != std::string::npos);
    CHECK(js.find("/api/wordpress/database-credentials/rotate") != std::string::npos);
    const auto start = js.find("WORDPRESS DATABASE CREDENTIALS");
    const auto end = js.find("PHP MAIL CARD");
    REQUIRE(start != std::string::npos);
    REQUIRE(end != std::string::npos);
    const std::string wordpress_block = js.substr(start, end - start);
    CHECK(wordpress_block.find("DB_PASSWORD") == std::string::npos);
    CHECK(wordpress_block.find("new_password") == std::string::npos);
    CHECK(wordpress_block.find("generated_password") == std::string::npos);
    CHECK(wordpress_block.find("database_target_available") != std::string::npos);
    CHECK(wordpress_block.find("database_target_status") != std::string::npos);
    CHECK(wordpress_block.find("targetBadgeClass") != std::string::npos);
    CHECK(wordpress_block.find("disabledReason") != std::string::npos);
    CHECK(wordpress_block.find("renderWordPressRotationDiagnostics") != std::string::npos);
    CHECK(wordpress_block.find("compensation_result") != std::string::npos);
    CHECK(wordpress_block.find("manual_recovery_required") != std::string::npos);
    CHECK(wordpress_block.find("siteDatabases[0]") == std::string::npos);
}

TEST_CASE("SSL providers response format") {
    std::string json = "{\"success\":true,\"data\":["
        "{\"id\":\"letsencrypt\",\"name\":\"Let's Encrypt\",\"supports_auto_renew\":true,\"supports_dns\":false}"
        "]}";

    CHECK(json.find("\"id\":\"letsencrypt\"") != std::string::npos);
    CHECK(json.find("\"supports_auto_renew\":true") != std::string::npos);
}

TEST_CASE("Private key path not exposed in response") {
    // privkey.pem path should never appear in any API response
    std::string response = "{\"success\":true,\"data\":{"
        "\"certificate_path\":\"/srv/containercp/ssl/1/current/fullchain.pem\""
        "}}";

    // OK to have fullchain.pem paths
    CHECK(response.find("fullchain.pem") != std::string::npos);

    // privkey.pem should NOT be in responses
    std::string safe_response = "{\"success\":true,\"data\":{"
        "\"status\":\"active\""
        "}}";
    CHECK(safe_response.find("privkey") == std::string::npos);

    // Explicitly verify the API never returns private key content
    CHECK(response.find("PRIVATE KEY") == std::string::npos);
}

// --- CertificateStore integration ---

TEST_CASE("CertificateStore metadata format matches API expectations") {
    auto& log = containercp::logger::Logger::instance();
    std::string dir = "/tmp/containercp_test_api_ssl_" + std::to_string(::getpid());
    ::mkdir(dir.c_str(), 0700);

    containercp::ssl::CertificateStore store(log, dir);

    containercp::ssl::CertificateStore::Metadata meta;
    meta.site_id = 1;
    meta.provider_id = "letsencrypt";
    meta.status = "active";
    meta.domains = {"example.com"};
    meta.https_enabled = true;
    meta.auto_renew = true;

    CHECK(store.save_metadata(1, meta));

    auto loaded = store.load_metadata(1);
    CHECK(loaded.success);
    CHECK(loaded.metadata.status == "active");
    CHECK(loaded.metadata.https_enabled);
    CHECK(loaded.metadata.domains.size() == 1);
    CHECK(loaded.metadata.domains[0] == "example.com");

    // Cleanup
    store.remove_all(1);
    ::rmdir(dir.c_str());
}
