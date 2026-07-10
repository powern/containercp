#include "runtime/HealthReport.h"

#include "doctest/doctest.h"

TEST_CASE("HealthRegistry register and check") {
    containercp::runtime::HealthRegistry reg;

    reg.register_check("test", []() -> containercp::runtime::HealthReport {
        containercp::runtime::HealthReport r;
        r.status = "ok";
        r.services.push_back({"svc1", "ok", "running"});
        return r;
    });

    auto r = reg.check("test");
    CHECK(r.status == "ok");
    CHECK(r.services.size() == 1);
    CHECK(r.services[0].name == "svc1");
    CHECK(r.services[0].status == "ok");
}

TEST_CASE("HealthRegistry unknown check returns error") {
    containercp::runtime::HealthRegistry reg;

    auto r = reg.check("nonexistent");
    CHECK(r.status == "error");
}

TEST_CASE("HealthRegistry check_all aggregates") {
    containercp::runtime::HealthRegistry reg;

    reg.register_check("a", []() -> containercp::runtime::HealthReport {
        containercp::runtime::HealthReport r;
        r.status = "ok";
        return r;
    });

    reg.register_check("b", []() -> containercp::runtime::HealthReport {
        containercp::runtime::HealthReport r;
        r.status = "error";
        return r;
    });

    auto results = reg.check_all();
    CHECK(results.size() == 2);

    // Order follows map iteration (alphabetical by key)
    CHECK(results[0].first == "a");
    CHECK(results[0].second.status == "ok");
    CHECK(results[1].first == "b");
    CHECK(results[1].second.status == "error");
}

TEST_CASE("HealthRegistry empty check_all") {
    containercp::runtime::HealthRegistry reg;
    CHECK(reg.check_all().empty());
}

TEST_CASE("HealthRegistry re-register replaces check") {
    containercp::runtime::HealthRegistry reg;

    reg.register_check("x", []() -> containercp::runtime::HealthReport {
        containercp::runtime::HealthReport r;
        r.status = "error";
        return r;
    });

    reg.register_check("x", []() -> containercp::runtime::HealthReport {
        containercp::runtime::HealthReport r;
        r.status = "ok";
        return r;
    });

    auto r = reg.check("x");
    CHECK(r.status == "ok");
}
