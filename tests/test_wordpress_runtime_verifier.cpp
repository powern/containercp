#include "wordpress/WordPressRuntimeVerifier.h"

#include "doctest/doctest.h"

#include <string>
#include <vector>

using namespace containercp::wordpress;

namespace {

struct FakeWordPressRuntimeRunner : WordPressRuntimeCommandRunner {
    containercp::runtime::CommandResult result;
    mutable std::vector<std::string> args;
    mutable std::string workdir;

    containercp::runtime::CommandResult run(const std::vector<std::string>& command_args,
                                            const std::string& command_workdir = "") const override {
        args = command_args;
        workdir = command_workdir;
        return result;
    }
};

} // namespace

TEST_CASE("WordPressRuntimeVerifier executes scoped PHP database verification") {
    FakeWordPressRuntimeRunner runner;
    runner.result.exit_code = 0;
    WordPressRuntimeVerifier verifier(runner);

    const auto result = verifier.verify_database_access({"/srv/containercp/sites/example.com",
                                                         "/srv/containercp/sites/example.com/public",
                                                         "/srv/containercp/sites/example.com/public/wp-config.php"});

    CHECK(result.success);
    CHECK(result.code == "wordpress_php_verification_ok");
    REQUIRE(runner.args.size() == 12);
    CHECK(runner.args[0] == "docker");
    CHECK(runner.args[1] == "compose");
    CHECK(runner.args[2] == "--project-directory");
    CHECK(runner.args[3] == "/srv/containercp/sites/example.com");
    CHECK(runner.args[4] == "exec");
    CHECK(runner.args[5] == "-T");
    CHECK(runner.args[6] == "php");
    CHECK(runner.args[7] == "php");
    CHECK(runner.args[8] == "-d");
    CHECK(runner.args[9] == "display_errors=0");
    CHECK(runner.args[10] == "-r");
    CHECK(runner.args[11].find("/var/www/html/wp-config.php") != std::string::npos);
    CHECK(runner.workdir.empty());
}

TEST_CASE("WordPressRuntimeVerifier rejects config path outside document root") {
    FakeWordPressRuntimeRunner runner;
    runner.result.exit_code = 0;
    WordPressRuntimeVerifier verifier(runner);

    const auto result = verifier.verify_database_access({"/srv/containercp/sites/example.com",
                                                         "/srv/containercp/sites/example.com/public",
                                                         "/srv/containercp/sites/example.com/private/wp-config.php"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "unsafe_config_path");
    CHECK(runner.args.empty());
}

TEST_CASE("WordPressRuntimeVerifier does not expose command stderr on failure") {
    FakeWordPressRuntimeRunner runner;
    runner.result.exit_code = 4;
    runner.result.err = "database failed with secret-password";
    WordPressRuntimeVerifier verifier(runner);

    const auto result = verifier.verify_database_access({"/srv/containercp/sites/example.com",
                                                         "/srv/containercp/sites/example.com/public",
                                                         "/srv/containercp/sites/example.com/public/wp-config.php"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "wordpress_php_verification_failed");
    CHECK(result.message.find("secret-password") == std::string::npos);
}

TEST_CASE("WordPressRuntimeVerifier generated script contains no credential literals") {
    const std::string script = wordpress_runtime_verification_script("/var/www/html/wp-config.php");

    CHECK(script.find("DB_PASSWORD") != std::string::npos);
    CHECK(script.find("secret-password") == std::string::npos);
    CHECK(script.find("mysqli") != std::string::npos);
}

TEST_CASE("WordPressRuntimeVerifier escapes PHP config path literal") {
    const std::string script = wordpress_runtime_verification_script("/var/www/html/weird'path/wp-config.php");

    CHECK(script.find("weird\\'path") != std::string::npos);
}
