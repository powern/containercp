#include "api/Router.h"
#include "api/Response.h"
#include "api/JsonFormatter.h"

#include <string>

#include "doctest/doctest.h"

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
    s.name = "test.com";
    s.domain = "test.com";
    s.owner = "admin";
    s.node_id = 1;

    auto json = containercp::api::JsonFormatter::sites({s});
    CHECK(json.find("\"domain\":\"test.com\"") != std::string::npos);
    CHECK(json.find("\"owner\":\"admin\"") != std::string::npos);
}

TEST_CASE("JsonFormatter empty users") {
    std::vector<containercp::user::User> empty;
    auto json = containercp::api::JsonFormatter::users(empty);
    CHECK(json == "[]");
}

TEST_CASE("JsonFormatter empty domains") {
    std::vector<containercp::domain::Domain> empty;
    auto json = containercp::api::JsonFormatter::domains(empty);
    CHECK(json == "[]");
}

TEST_CASE("JsonFormatter empty proxies") {
    std::vector<containercp::proxy::ReverseProxy> empty;
    auto json = containercp::api::JsonFormatter::proxies(empty);
    CHECK(json == "[]");
}

TEST_CASE("JsonFormatter empty ssl") {
    std::vector<containercp::ssl::SslCertificate> empty;
    auto json = containercp::api::JsonFormatter::ssl_certificates(empty);
    CHECK(json == "[]");
}

TEST_CASE("Response to_string format") {
    containercp::api::Response resp;
    resp.status_code = 200;
    resp.body = "{\"ok\":true}";
    auto str = resp.to_string();
    CHECK(str.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(str.find("Content-Type: application/json") != std::string::npos);
    CHECK(str.find("{\"ok\":true}") != std::string::npos);
}
