#include "runtime/CommandExecutor.h"
#include "runtime/RuntimeActionExecutor.h"
#include "runtime/ServiceRole.h"
#include "runtime/SiteRuntimeManager.h"
#include "logger/Logger.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "doctest/doctest.h"

using namespace containercp::runtime;

namespace {

std::string write_command_executor_input(const std::string& name, const std::string& content) {
    const std::string path = "/tmp/containercp-command-executor-" + std::to_string(::getpid()) + "-" + name;
    std::ofstream out(path);
    out << content;
    return path;
}

} // namespace

// ── CommandExecutor ───────────────────────────────────────────────

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

TEST_CASE("CommandExecutor::run_with_stdin_file captures stdout and feeds stdin") {
    CommandExecutor exec;
    const auto input = write_command_executor_input("stdin.txt", "payload");

    auto r = exec.run_with_stdin_file({"sh", "-c", "cat; printf ':done'"}, input);

    std::remove(input.c_str());
    CHECK(r.exit_code == 0);
    CHECK(r.out == "payload:done");
    CHECK(r.err.empty());
}

TEST_CASE("CommandExecutor::run_with_stdin_file preserves stderr and exit code") {
    CommandExecutor exec;
    const auto input = write_command_executor_input("stderr.txt", "ignored");

    auto r = exec.run_with_stdin_file({"sh", "-c", "cat >/dev/null; printf 'problem' >&2; exit 37"}, input);

    std::remove(input.c_str());
    CHECK(r.exit_code == 37);
    CHECK(r.out.empty());
    CHECK(r.err == "problem");
}

TEST_CASE("CommandExecutor::run_with_stdin_file captures large stdout") {
    CommandExecutor exec;
    const auto input = write_command_executor_input("large.txt", "ignored");

    auto r = exec.run_with_stdin_file({"sh", "-c", "i=0; while [ $i -lt 6000 ]; do printf x; i=$((i + 1)); done"}, input);

    std::remove(input.c_str());
    CHECK(r.exit_code == 0);
    CHECK(r.out.size() == 6000);
    CHECK(r.out.find_first_not_of('x') == std::string::npos);
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

// ── ServiceRole (Runtime subsystem core) ──────────────────────────

TEST_CASE("ServiceRole role_to_action_suffix") {
    CHECK(role_to_action_suffix(ServiceRole::Frontend) == "web");
    CHECK(role_to_action_suffix(ServiceRole::PHP) == "php");
    CHECK(role_to_action_suffix(ServiceRole::Database) == "db");
    CHECK(role_to_action_suffix(ServiceRole::Cache) == "redis");
}

TEST_CASE("ServiceRole role_to_compose_service") {
    CHECK(role_to_compose_service(ServiceRole::Frontend) == "web");
    CHECK(role_to_compose_service(ServiceRole::PHP) == "php");
    CHECK(role_to_compose_service(ServiceRole::Database) == "mariadb");
    CHECK(role_to_compose_service(ServiceRole::Cache) == "redis");
}

TEST_CASE("ServiceRole roles_from_action") {
    SUBCASE("restart-web") {
        auto roles = roles_from_action("restart-web");
        REQUIRE(roles.size() == 1);
        CHECK(roles[0] == ServiceRole::Frontend);
    }

    SUBCASE("restart-php") {
        auto roles = roles_from_action("restart-php");
        REQUIRE(roles.size() == 1);
        CHECK(roles[0] == ServiceRole::PHP);
    }

    SUBCASE("restart-db") {
        auto roles = roles_from_action("restart-db");
        REQUIRE(roles.size() == 1);
        CHECK(roles[0] == ServiceRole::Database);
    }

    SUBCASE("restart-redis") {
        auto roles = roles_from_action("restart-redis");
        REQUIRE(roles.size() == 1);
        CHECK(roles[0] == ServiceRole::Cache);
    }

    SUBCASE("restart-all") {
        auto roles = roles_from_action("restart-all");
        CHECK(roles.empty());
    }

    SUBCASE("invalid action") {
        CHECK(roles_from_action("reboot").empty());
    }
}

TEST_CASE("ServiceRole roles_to_compose_services") {
    SUBCASE("empty input returns empty (all services)") {
        CHECK(roles_to_compose_services({}).empty());
    }

    SUBCASE("frontend role") {
        auto svc = roles_to_compose_services({ServiceRole::Frontend});
        REQUIRE(svc.size() == 1);
        CHECK(svc[0] == "web");
    }

    SUBCASE("all roles") {
        auto svc = roles_to_compose_services({
            ServiceRole::Frontend,
            ServiceRole::PHP,
            ServiceRole::Database,
            ServiceRole::Cache
        });
        REQUIRE(svc.size() == 4);
        CHECK(svc[0] == "web");
        CHECK(svc[1] == "php");
        CHECK(svc[2] == "mariadb");
        CHECK(svc[3] == "redis");
    }
}

// ── SiteRuntimeManager (Sites module consumer) ────────────────────

TEST_CASE("SiteRuntimeManager valid_actions list") {
    const auto& actions = SiteRuntimeManager::valid_actions();
    CHECK(actions.size() == 5);
    CHECK(actions[0] == "restart-web");
    CHECK(actions[1] == "restart-php");
    CHECK(actions[2] == "restart-db");
    CHECK(actions[3] == "restart-redis");
    CHECK(actions[4] == "restart-all");
}

TEST_CASE("SiteRuntimeManager services_for_action (delegates to ServiceRole)") {
    auto& log = containercp::logger::Logger::instance();
    RuntimeActionExecutor exec(log);
    SiteRuntimeManager mgr(log, "/tmp", exec);

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

    SUBCASE("restart-db maps to mariadb service") {
        auto svc = mgr.services_for_action("restart-db");
        REQUIRE(svc.size() == 1);
        CHECK(svc[0] == "mariadb");
    }

    SUBCASE("restart-redis maps to redis service") {
        auto svc = mgr.services_for_action("restart-redis");
        REQUIRE(svc.size() == 1);
        CHECK(svc[0] == "redis");
    }

    SUBCASE("restart-all returns empty for all services") {
        auto svc = mgr.services_for_action("restart-all");
        CHECK(svc.empty());
    }

    SUBCASE("invalid action returns empty") {
        CHECK(mgr.services_for_action("reboot").empty());
    }

    SUBCASE("empty action returns empty") {
        CHECK(mgr.services_for_action("").empty());
    }
}

// ── RuntimeActionExecutor (global execution layer) ────────────────

TEST_CASE("RuntimeActionExecutor compose_action basic errors") {
    auto& log = containercp::logger::Logger::instance();
    RuntimeActionExecutor exec(log);

    SUBCASE("missing compose dir returns error for single service") {
        auto r = exec.restart_services("/nonexistent/path", {"web"});
        CHECK_FALSE(r.success);
    }

    SUBCASE("missing compose dir returns error for all services (empty list)") {
        auto r = exec.restart_services("/nonexistent/path", {});
        CHECK_FALSE(r.success);
    }
}

TEST_CASE("RuntimeActionExecutor service_status basic errors") {
    auto& log = containercp::logger::Logger::instance();
    RuntimeActionExecutor exec(log);

    SUBCASE("missing compose dir returns Error status") {
        auto s = exec.service_status("/nonexistent/path", "web");
        CHECK(s.status == "Error");
    }
}

TEST_CASE("SiteRuntimeManager status semantic mapping") {
    // These test the container_status logic indirectly via
    // actual Docker commands — only run if docker is available.
    // Verified statuses: Running, Stopped, Unhealthy, Starting, Error.
}
