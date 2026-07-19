#include "database/MariaDBCredentialProvider.h"

#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace containercp::database;
using namespace containercp;

namespace {

struct FakeMariaDBRunner : MariaDBProcessRunner {
    runtime::CommandResult result;
    mutable std::vector<std::string> last_args;
    mutable std::string last_stdin_file;
    mutable std::string last_stdin_content;

    runtime::CommandResult run_with_stdin_file(const std::vector<std::string>& args,
                                               const std::string& stdin_file,
                                               const std::string& workdir = "") const override {
        (void)workdir;
        last_args = args;
        last_stdin_file = stdin_file;
        std::ifstream in(stdin_file, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        last_stdin_content = buffer.str();
        return result;
    }
};

bool args_contain(const std::vector<std::string>& args, const std::string& needle) {
    for (const auto& arg : args) {
        if (arg.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("MariaDBCredentialProvider changes password without secret argv exposure") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.change_password(
        {"/srv/containercp/sites/example.com/docker-compose.yml", "mariadb"},
        {"ccp_admin", "admin$secret", "localhost"},
        {"wp_user", "%"},
        "new'p\\ass$word");

    CHECK(result.success);
    CHECK(result.code == "password_changed");
    CHECK_FALSE(args_contain(runner.last_args, "admin$secret"));
    CHECK_FALSE(args_contain(runner.last_args, "new'p"));
    CHECK(runner.last_stdin_content.find("password=admin$secret") != std::string::npos);
    CHECK(runner.last_stdin_content.find("ALTER USER 'wp_user'@'%' IDENTIFIED BY 'new\\'p\\\\ass$word'") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(runner.last_stdin_file));
}

TEST_CASE("MariaDBCredentialProvider verifies target user password through stdin transport") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.verify_password({"compose.yml", "mariadb"}, {"wp_user", "localhost"}, "current-secret");

    CHECK(result.success);
    CHECK(result.code == "verified");
    CHECK_FALSE(args_contain(runner.last_args, "current-secret"));
    CHECK(runner.last_stdin_content.find("user=wp_user") != std::string::npos);
    CHECK(runner.last_stdin_content.find("password=current-secret") != std::string::npos);
    CHECK(runner.last_stdin_content.find("SELECT 1;") != std::string::npos);
}

TEST_CASE("MariaDBCredentialProvider restores password with same safe command path") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.restore_password({"compose.yml", "mariadb"}, {"admin", "admin-secret"}, {"wp_user", "localhost"}, "old-secret");

    CHECK(result.success);
    CHECK(result.code == "password_restored");
    CHECK_FALSE(args_contain(runner.last_args, "old-secret"));
    CHECK(runner.last_stdin_content.find("IDENTIFIED BY 'old-secret'") != std::string::npos);
}

TEST_CASE("MariaDBCredentialProvider redacts command failures") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 1;
    runner.result.err = "Access denied for password new-secret";
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.change_password({"compose.yml", "mariadb"}, {"admin", "admin-secret"}, {"wp_user", "%"}, "new-secret");

    CHECK_FALSE(result.success);
    CHECK(result.code == "mariadb_command_failed");
    CHECK(result.message.find("new-secret") == std::string::npos);
    CHECK(result.message.find("admin-secret") == std::string::npos);
    CHECK_FALSE(std::filesystem::exists(runner.last_stdin_file));
}

TEST_CASE("MariaDBCredentialProvider builds shared-user detection query without password argv") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.detect_shared_user({"compose.yml", "mariadb"}, {"admin", "admin-secret"}, {"shared'user", "%"});

    CHECK(result.success);
    CHECK(result.code == "shared_user_checked");
    CHECK_FALSE(args_contain(runner.last_args, "admin-secret"));
    CHECK(runner.last_stdin_content.find("WHERE User = 'shared\\'user'") != std::string::npos);
}

TEST_CASE("MariaDBCredentialProvider rejects incomplete target") {
    FakeMariaDBRunner runner;
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.verify_password({"", "mariadb"}, {"wp_user", "%"}, "secret");

    CHECK_FALSE(result.success);
    CHECK(result.code == "target_invalid");
    CHECK(runner.last_args.empty());
}
