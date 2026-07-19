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

std::string shared_output(int exact, int username_identities, int other_hosts, int schema_grants) {
    return "exact_identity\t" + std::to_string(exact) + "\n" +
           "username_identities\t" + std::to_string(username_identities) + "\n" +
           "other_host_identities\t" + std::to_string(other_hosts) + "\n" +
           "schema_grants\t" + std::to_string(schema_grants) + "\n";
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
        "new.Pass-123$word");

    CHECK(result.success);
    CHECK(result.code == "password_changed");
    CHECK_FALSE(args_contain(runner.last_args, "admin$secret"));
    CHECK_FALSE(args_contain(runner.last_args, "new.Pass-123"));
    CHECK(runner.last_stdin_content.find("CONTAINERCP-MARIADB-FRAME-V1\n") == 0);
    CHECK(runner.last_stdin_content.find("password=admin$secret") != std::string::npos);
    CHECK(runner.last_stdin_content.find("--CONTAINERCP-SQL--") == std::string::npos);
    CHECK(runner.last_stdin_content.find("ALTER USER 'wp_user'@'%' IDENTIFIED BY 'new.Pass-123$word'") != std::string::npos);
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

TEST_CASE("MariaDBCredentialProvider assesses one exact non-shared user host") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    runner.result.out = shared_output(1, 1, 0, 1);
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.detect_shared_user({"compose.yml", "mariadb"}, {"admin", "admin-secret"}, {"wp_user", "localhost"});

    CHECK(result.success);
    CHECK(result.code == "shared_user_checked");
    CHECK(result.shared_assessment.state == MariaDBSharedCredentialAssessmentState::NotShared);
    CHECK_FALSE(result.shared_user);
    CHECK(result.shared_assessment.identity.user == "wp_user");
    CHECK(result.shared_assessment.identity.host == "localhost");
    CHECK(result.shared_assessment.exact_identity_exists);
    CHECK(result.shared_assessment.exact_identity_count == 1);
    CHECK_FALSE(args_contain(runner.last_args, "admin-secret"));
    CHECK(runner.last_stdin_content.find("WHERE User = 'wp_user' AND Host = 'localhost'") != std::string::npos);
    CHECK(runner.last_stdin_content.find("COUNT(DISTINCT Db)") != std::string::npos);
}

TEST_CASE("MariaDBCredentialProvider marks exact identity shared when it has several schema grants") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    runner.result.out = shared_output(1, 1, 0, 2);
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.detect_shared_user({"compose.yml", "mariadb"}, {"admin", "admin-secret"}, {"wp_user", "%"});

    CHECK(result.success);
    CHECK(result.shared_assessment.state == MariaDBSharedCredentialAssessmentState::Shared);
    CHECK(result.shared_user);
    CHECK(result.shared_assessment.schema_grant_count == 2);
    CHECK(result.message.find("admin-secret") == std::string::npos);
}

TEST_CASE("MariaDBCredentialProvider distinguishes same username with different host values") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    runner.result.out = shared_output(1, 2, 1, 1);
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.detect_shared_user({"compose.yml", "mariadb"}, {"admin", "admin-secret"}, {"wp_user", "%"});

    CHECK(result.success);
    CHECK(result.shared_assessment.state == MariaDBSharedCredentialAssessmentState::MultipleHostIdentities);
    CHECK(result.shared_user);
    CHECK(result.shared_assessment.username_has_other_hosts);
    CHECK(result.shared_assessment.other_host_identity_count == 1);
}

TEST_CASE("MariaDBCredentialProvider reports missing identity without treating it as not shared") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    runner.result.out = shared_output(0, 0, 0, 0);
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.detect_shared_user({"compose.yml", "mariadb"}, {"admin", "admin-secret"}, {"missing_user", "localhost"});

    CHECK(result.success);
    CHECK(result.shared_assessment.state == MariaDBSharedCredentialAssessmentState::IdentityMissing);
    CHECK(result.shared_user);
    CHECK_FALSE(result.shared_assessment.exact_identity_exists);
}

TEST_CASE("MariaDBCredentialProvider fails closed on malformed shared-user output") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    runner.result.out = "exact_identity\t1\textra\nusername_identities\t1\nother_host_identities\t0\nschema_grants\t1\n";
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.detect_shared_user({"compose.yml", "mariadb"}, {"admin", "admin-secret"}, {"wp_user", "%"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "shared_user_assessment_invalid");
    CHECK(result.shared_assessment.state == MariaDBSharedCredentialAssessmentState::Unknown);
    CHECK(result.shared_user);
    CHECK(result.message.find("admin-secret") == std::string::npos);
    CHECK(result.message.find("exact_identity") == std::string::npos);
}

TEST_CASE("MariaDBCredentialProvider fails closed on empty shared-user output") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    runner.result.out = "";
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.detect_shared_user({"compose.yml", "mariadb"}, {"admin", "admin-secret"}, {"wp_user", "%"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "shared_user_assessment_invalid");
    CHECK(result.shared_assessment.state == MariaDBSharedCredentialAssessmentState::Unknown);
    CHECK(result.shared_user);
}

TEST_CASE("MariaDBCredentialProvider fails closed on shared-user command failure") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 1;
    runner.result.err = "Access denied for password admin-secret";
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.detect_shared_user({"compose.yml", "mariadb"}, {"admin", "admin-secret"}, {"wp_user", "%"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "mariadb_command_failed");
    CHECK(result.shared_assessment.state == MariaDBSharedCredentialAssessmentState::Unknown);
    CHECK(result.shared_user);
    CHECK(result.message.find("admin-secret") == std::string::npos);
}

TEST_CASE("MariaDBCredentialProvider rejects incomplete target") {
    FakeMariaDBRunner runner;
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.verify_password({"", "mariadb"}, {"wp_user", "%"}, "secret");

    CHECK_FALSE(result.success);
    CHECK(result.code == "target_invalid");
    CHECK(runner.last_args.empty());
}

TEST_CASE("MariaDBCredentialProvider rejects unsafe transport values before writing secret files") {
    const std::vector<std::string> invalid_passwords = {
        "line\nbreak",
        "carriage\rreturn",
        std::string("nul") + '\0' + "byte",
        "tab\tchar",
        "hash#comment",
        "semi;colon",
        "[section]",
        " leading",
        "trailing ",
        "equals=value",
        "back\\slash",
        "quote'char",
        "double\"quote",
        "--CONTAINERCP-SQL--",
        std::string(300, 'a'),
    };

    for (const auto& password : invalid_passwords) {
        FakeMariaDBRunner runner;
        runner.result.exit_code = 0;
        MariaDBCredentialProvider provider(runner);

        const auto result = provider.change_password({"compose.yml", "mariadb"}, {"admin", "adminSecret1"}, {"wp_user", "%"}, password);

        CHECK_FALSE(result.success);
        CHECK(result.code == "credential_transport_invalid");
        CHECK(result.message.find(password) == std::string::npos);
        CHECK(runner.last_args.empty());
        CHECK(runner.last_stdin_file.empty());
    }
}
