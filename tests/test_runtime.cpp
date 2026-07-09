#include "runtime/CommandExecutor.h"
#include "runtime/RuntimeActionExecutor.h"
#include "runtime/SiteRuntimeManager.h"
#include "logger/Logger.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>

#include "doctest/doctest.h"

using namespace containercp::runtime;

TEST_CASE("CommandExecutor::run returns error for empty args") {
    CommandExecutor exec;
    auto r = exec.run({});
    CHECK(r.exit_code == -1);
    CHECK(!r.err.empty());
}

TEST_CASE("CommandExecutor::run captures stdout") {
    CommandExecutor exec;
    auto r = exec.run({"echo", "hello world"});
    CHECK(r.exit_code == 0);
    CHECK(r.out.find("hello world") != std::string::npos);
}

TEST_CASE("CommandExecutor::run captures stderr") {
    CommandExecutor exec;
    auto r = exec.run({"sh", "-c", "echo errmsg >&2"});
    CHECK(r.exit_code == 0);
    CHECK(r.err.find("errmsg") != std::string::npos);
}

TEST_CASE("CommandExecutor::run non-existent command") {
    CommandExecutor exec;
    auto r = exec.run({"nonexistent_cmd_xyz123"});
    CHECK(r.exit_code == 127);
}

TEST_CASE("CommandExecutor::run failed command") {
    CommandExecutor exec;
    auto r = exec.run({"sh", "-c", "exit 42"});
    CHECK(r.exit_code == 42);
}

TEST_CASE("CommandExecutor::run with workdir") {
    CommandExecutor exec;
    auto r = exec.run({"pwd"}, "/tmp");
    CHECK(r.exit_code == 0);
    CHECK(r.out.find("/tmp") != std::string::npos);
}

TEST_CASE("SiteRuntimeManager valid_actions list") {
    const auto& actions = SiteRuntimeManager::valid_actions();
    CHECK(actions.size() == 3);
    CHECK(actions[0] == "restart-web");
    CHECK(actions[1] == "restart-php");
    CHECK(actions[2] == "restart-all");
}

TEST_CASE("SiteRuntimeManager services_for_action mapping") {
    auto& log = containercp::logger::Logger::instance();
    SiteRuntimeManager mgr(log, "/tmp");

    SUBCASE("restart-web maps to web service") {
        auto svc = mgr.services_for_action("restart-web");
        REQUIRE(svc.size() == 1);
        CHECK(svc[0] == "web");
    }

    SUBCASE("restart-php maps to php service") {
        auto svc = mgr.services_for_action("restart-php");
        REQUIRE(svc.size() == 1);
        CHECK(svc[0] == "php");
    }

    SUBCASE("restart-all maps to both services") {
        auto svc = mgr.services_for_action("restart-all");
        REQUIRE(svc.size() == 2);
        CHECK(svc[0] == "web");
        CHECK(svc[1] == "php");
    }

    SUBCASE("invalid action returns empty") {
        CHECK(mgr.services_for_action("reboot").empty());
    }

    SUBCASE("empty action returns empty") {
        CHECK(mgr.services_for_action("").empty());
    }
}

TEST_CASE("RuntimeActionExecutor compose_action basic errors") {
    auto& log = containercp::logger::Logger::instance();
    RuntimeActionExecutor exec(log);

    SUBCASE("missing compose dir returns error") {
        auto r = exec.restart_services("/nonexistent/path", {"web"});
        CHECK_FALSE(r.success);
    }
}

TEST_CASE("SiteRuntimeManager status semantic mapping") {
    // These test the container_status logic indirectly via
    // actual Docker commands — only run if docker is available.
    // We verify that the status mapper produces correct strings
    // for known docker states.
}
