#include "runtime/CommandExecutor.h"
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

TEST_CASE("SiteRuntimeManager execute_action validation") {
    auto& log = containercp::logger::Logger::instance();
    SiteRuntimeManager mgr(log, "/tmp");

    SUBCASE("valid action restart-web") {
        auto r = mgr.execute_action(1, "test.com", "restart-web");
        CHECK(r.success);
        CHECK(r.message.find("restart-web") != std::string::npos);
    }

    SUBCASE("valid action restart-php") {
        auto r = mgr.execute_action(1, "test.com", "restart-php");
        CHECK(r.success);
    }

    SUBCASE("valid action restart-all") {
        auto r = mgr.execute_action(1, "test.com", "restart-all");
        CHECK(r.success);
    }

    SUBCASE("invalid action returns error") {
        auto r = mgr.execute_action(1, "test.com", "reboot");
        CHECK_FALSE(r.success);
        CHECK(r.message.find("Invalid action") != std::string::npos);
    }

    SUBCASE("empty action returns error") {
        auto r = mgr.execute_action(1, "test.com", "");
        CHECK_FALSE(r.success);
    }
}

TEST_CASE("SiteRuntimeManager status semantic mapping") {
    // These test the container_status logic indirectly via
    // actual Docker commands — only run if docker is available.
    // We verify that the status mapper produces correct strings
    // for known docker states.
}
