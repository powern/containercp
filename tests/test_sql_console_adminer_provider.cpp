#include "doctest/doctest.h"

#include "sqlconsole/AdminerSqlConsoleProvider.h"

#include <string>
#include <vector>

using namespace containercp::sqlconsole;

namespace {

class CapturingSqlConsoleRunner : public SqlConsoleRuntimeRunner {
public:
    mutable std::vector<std::string> last_args;
    int exit_code = 0;
    std::string out = "true\n";

    containercp::runtime::CommandResult run(const std::vector<std::string>& args) const override {
        last_args = args;
        return {exit_code, out, {}};
    }
};

SqlConsoleProviderLaunchRequest launch_request() {
    SqlConsoleProviderLaunchRequest request;
    request.launch_id = "0123456789abcdef0123456789abcdef";
    request.site_id = 12;
    request.database_id = 34;
    request.site_domain = "Example.Test";
    request.site_root = "/srv/containercp/sites/example.test";
    request.provider = "adminer";
    request.adminer_sso_plugin_path = "/srv/containercp/sqlconsole/adminer/containercp-sso.php";
    request.internal_token_path = "/srv/containercp/sqlconsole/adminer/internal-token";
    return request;
}

bool args_contain(const std::vector<std::string>& args, const std::string& value) {
    for (const auto& arg : args) {
        if (arg.find(value) != std::string::npos) return true;
    }
    return false;
}

bool args_equal(const std::vector<std::string>& args, const std::string& value) {
    for (const auto& arg : args) {
        if (arg == value) return true;
    }
    return false;
}

} // namespace

TEST_CASE("Adminer SQL Console provider builds isolated docker run command") {
    CapturingSqlConsoleRunner runner;
    AdminerSqlConsoleProvider provider(runner, "adminer:latest");

    const auto result = provider.start(launch_request());

    REQUIRE(result.success);
    CHECK(result.container_name == "ccp-sqlconsole-0123456789abcdef01234567");
    CHECK(result.upstream == "http://ccp-sqlconsole-0123456789abcdef01234567:8080");
    REQUIRE(runner.last_args.size() > 10);
    CHECK(runner.last_args[0] == "docker");
    CHECK(args_contain(runner.last_args, "run"));
    CHECK(args_contain(runner.last_args, "--rm"));
    CHECK(args_contain(runner.last_args, "--network"));
    CHECK(args_contain(runner.last_args, "exampletest_containercp-site-12"));
    CHECK(args_contain(runner.last_args, "host.docker.internal:host-gateway"));
    CHECK(args_contain(runner.last_args, "/var/www/html/plugins-enabled/containercp-sso.php:ro"));
    CHECK(args_contain(runner.last_args, "/run/containercp/sql-console-token:ro"));
    CHECK(args_contain(runner.last_args, "containercp.sql_console.provider=adminer"));
    CHECK(args_contain(runner.last_args, "containercp.sql_console.launch_id=0123456789abcdef0123456789abcdef"));
    CHECK(args_contain(runner.last_args, "containercp.site.id=12"));
    CHECK(args_contain(runner.last_args, "containercp.database.id=34"));
    CHECK_FALSE(args_equal(runner.last_args, "-p"));
    CHECK_FALSE(args_equal(runner.last_args, "--publish"));
    CHECK_FALSE(args_contain(runner.last_args, "password"));
    CHECK_FALSE(args_contain(runner.last_args, "secret"));
}

TEST_CASE("Adminer SQL Console provider stop and status target generated container only") {
    CapturingSqlConsoleRunner runner;
    AdminerSqlConsoleProvider provider(runner);

    const auto stopped = provider.stop("0123456789abcdef0123456789abcdef");
    REQUIRE(stopped.success);
    CHECK(runner.last_args.size() == 3);
    CHECK(runner.last_args[0] == "docker");
    CHECK(runner.last_args[1] == "stop");
    CHECK(runner.last_args[2] == "ccp-sqlconsole-0123456789abcdef01234567");

    const auto status = provider.status("0123456789abcdef0123456789abcdef");
    REQUIRE(status.success);
    CHECK(args_contain(runner.last_args, "inspect"));
    CHECK(args_contain(runner.last_args, "{{.State.Running}}"));
}

TEST_CASE("Adminer SQL Console provider status fails when container is stopped") {
    CapturingSqlConsoleRunner runner;
    runner.out = "false\n";
    AdminerSqlConsoleProvider provider(runner);

    const auto status = provider.status("0123456789abcdef0123456789abcdef");

    CHECK_FALSE(status.success);
    CHECK(status.code == "adminer_not_running");
}

TEST_CASE("Adminer SQL Console provider rejects malformed launch requests") {
    CapturingSqlConsoleRunner runner;
    AdminerSqlConsoleProvider provider(runner);
    auto request = launch_request();
    request.launch_id = "not-valid";
    CHECK_FALSE(provider.start(request).success);
    CHECK(runner.last_args.empty());

    request = launch_request();
    request.provider = "other";
    CHECK_FALSE(provider.start(request).success);
}
