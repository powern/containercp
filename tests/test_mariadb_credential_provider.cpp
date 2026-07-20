#include "database/MariaDBCredentialProvider.h"

#include "doctest/doctest.h"

#include <filesystem>
#include <fstream>
#include <atomic>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

using namespace containercp::database;
using namespace containercp;

namespace {

struct FakeMariaDBRunner : MariaDBProcessRunner {
    runtime::CommandResult result;
    mutable std::vector<std::string> last_args;
    mutable std::string last_stdin_file;
    mutable std::string last_stdin_content;
    mutable std::vector<std::string> stdin_files;
    mutable std::mutex mutex;

    runtime::CommandResult run_with_stdin_file(const std::vector<std::string>& args,
                                               const std::string& stdin_file,
                                               const std::string& workdir = "") const override {
        (void)workdir;
        std::ifstream in(stdin_file, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        std::lock_guard<std::mutex> guard(mutex);
        last_args = args;
        last_stdin_file = stdin_file;
        last_stdin_content = buffer.str();
        stdin_files.push_back(stdin_file);
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

std::string shell_script_arg(const std::vector<std::string>& args) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] == "-c") {
            return args[i + 1];
        }
    }
    return {};
}

void check_defaults_file_option_is_first_mariadb_option(const std::vector<std::string>& args) {
    const auto script = shell_script_arg(args);
    const auto mariadb = script.find("mariadb ");
    REQUIRE(mariadb != std::string::npos);
    const auto defaults = script.find("--defaults-extra-file=", mariadb);
    REQUIRE(defaults != std::string::npos);
    CHECK(script.substr(mariadb, defaults - mariadb) == "mariadb ");
    CHECK(script.find("mariadb --batch") == std::string::npos);
    CHECK(script.find("--batch --raw --skip-column-names --defaults-extra-file") == std::string::npos);
}

std::string shared_output(int exact, int username_identities, int other_hosts, int schema_grants) {
    return "exact_identity\t" + std::to_string(exact) + "\n" +
           "username_identities\t" + std::to_string(username_identities) + "\n" +
           "other_host_identities\t" + std::to_string(other_hosts) + "\n" +
           "schema_grants\t" + std::to_string(schema_grants) + "\n";
}

struct ParsedMariaDBBundle {
    std::string defaults;
    std::string sql;
};

ParsedMariaDBBundle parse_bundle(const std::string& content) {
    std::istringstream stream(content);
    std::string magic;
    std::string conf_len_text;
    std::string sql_len_text;
    std::getline(stream, magic);
    std::getline(stream, conf_len_text);
    std::getline(stream, sql_len_text);
    const auto conf_len = static_cast<std::size_t>(std::stoull(conf_len_text));
    const auto sql_len = static_cast<std::size_t>(std::stoull(sql_len_text));
    ParsedMariaDBBundle bundle;
    bundle.defaults.resize(conf_len);
    stream.read(bundle.defaults.data(), static_cast<std::streamsize>(conf_len));
    bundle.sql.resize(sql_len);
    stream.read(bundle.sql.data(), static_cast<std::streamsize>(sql_len));
    return bundle;
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
    check_defaults_file_option_is_first_mariadb_option(runner.last_args);
    CHECK_FALSE(std::filesystem::exists(runner.last_stdin_file));
}

TEST_CASE("MariaDBCredentialProvider uses canonical defaults-file ordering for all SQL operations") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    MariaDBCredentialProvider provider(runner);

    SUBCASE("verify password") {
        const auto result = provider.verify_password({"compose.yml", "mariadb"}, {"wp_user", "localhost"}, "current-secret");
        CHECK(result.success);
        check_defaults_file_option_is_first_mariadb_option(runner.last_args);
    }

    SUBCASE("change password") {
        const auto result = provider.change_password({"compose.yml", "mariadb"}, {"admin", "admin-secret", "localhost"}, {"wp_user", "%"}, "new-secret");
        CHECK(result.success);
        check_defaults_file_option_is_first_mariadb_option(runner.last_args);
    }

    SUBCASE("restore password") {
        const auto result = provider.restore_password({"compose.yml", "mariadb"}, {"admin", "admin-secret", "localhost"}, {"wp_user", "%"}, "old-secret");
        CHECK(result.success);
        check_defaults_file_option_is_first_mariadb_option(runner.last_args);
    }

    SUBCASE("detect shared user") {
        runner.result.out = shared_output(1, 1, 0, 1);
        const auto result = provider.detect_shared_user({"compose.yml", "mariadb"}, {"admin", "admin-secret", "localhost"}, {"wp_user", "%"});
        CHECK(result.success);
        check_defaults_file_option_is_first_mariadb_option(runner.last_args);
    }
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

TEST_CASE("MariaDBCredentialProvider accepts imported passwords through escaped option-file transport") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    MariaDBCredentialProvider provider(runner);
    std::string password = " leading #;=[]'\"\\\n\t\r";
    password += "\xC3\xA9";

    const auto result = provider.verify_password({"compose.yml", "mariadb"}, {"wp_user", "localhost"}, password);

    CHECK(result.success);
    CHECK(result.code == "verified");
    const auto bundle = parse_bundle(runner.last_stdin_content);
    CHECK(bundle.defaults.find("password=\\sleading") != std::string::npos);
    CHECK(bundle.defaults.find("#;=[]'\"") != std::string::npos);
    CHECK(bundle.defaults.find("\\\\") != std::string::npos);
    CHECK(bundle.defaults.find("\\n") != std::string::npos);
    CHECK(bundle.defaults.find("\\t") != std::string::npos);
    CHECK(bundle.defaults.find("\\r") != std::string::npos);
    CHECK(bundle.defaults.find("\xC3\xA9") != std::string::npos);
    CHECK(bundle.sql == "SELECT 1;");
    CHECK_FALSE(args_contain(runner.last_args, password));
}

TEST_CASE("MariaDBCredentialProvider supports empty imported passwords for verification") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.verify_password({"compose.yml", "mariadb"}, {"wp_user", "localhost"}, "");

    CHECK(result.success);
    const auto bundle = parse_bundle(runner.last_stdin_content);
    CHECK(bundle.defaults.find("password=\n") != std::string::npos);
}

TEST_CASE("MariaDBCredentialProvider escapes imported passwords in ALTER USER SQL") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    MariaDBCredentialProvider provider(runner);
    const std::string password = "quote'back\\line\ncarriage\rtab\tsemi;space end";

    const auto result = provider.change_password({"compose.yml", "mariadb"}, {"admin", "admin secret", "localhost"}, {"wp_user", "%"}, password);

    CHECK(result.success);
    const auto bundle = parse_bundle(runner.last_stdin_content);
    CHECK(bundle.defaults.find("password=admin\\ssecret") != std::string::npos);
    CHECK(bundle.sql.find("IDENTIFIED BY 'quote\\'back\\\\line\\ncarriage\\rtab\\tsemi;space end'") != std::string::npos);
    CHECK_FALSE(args_contain(runner.last_args, password));
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

TEST_CASE("MariaDBCredentialProvider uses unique temporary secret files for concurrent operations") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    MariaDBCredentialProvider provider(runner);
    std::atomic<int> successes{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&provider, &successes, i]() {
            const auto result = provider.verify_password({"compose.yml", "mariadb"}, {"wp_user", "localhost"}, "secret-" + std::to_string(i));
            if (result.success) {
                ++successes;
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    std::set<std::string> unique_paths;
    CHECK(successes == 8);
    {
        std::lock_guard<std::mutex> guard(runner.mutex);
        REQUIRE(runner.stdin_files.size() == 8);
        unique_paths.insert(runner.stdin_files.begin(), runner.stdin_files.end());
    }
    CHECK(unique_paths.size() == 8);
    for (const auto& path : unique_paths) {
        CHECK_FALSE(std::filesystem::exists(path));
    }
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

TEST_CASE("MariaDBCredentialProvider parses captured shared-user SQL output") {
    FakeMariaDBRunner runner;
    runner.result.exit_code = 0;
    runner.result.out = "exact_identity\t1\n"
                        "username_identities\t1\n"
                        "other_host_identities\t0\n"
                        "schema_grants\t1\n";
    MariaDBCredentialProvider provider(runner);

    const auto result = provider.detect_shared_user({"compose.yml", "mariadb"}, {"admin", "admin-secret"}, {"wp_user", "%"});

    CHECK(result.success);
    CHECK(result.code == "shared_user_checked");
    CHECK(result.shared_assessment.state == MariaDBSharedCredentialAssessmentState::NotShared);
    CHECK(result.shared_assessment.exact_identity_count == 1);
    CHECK(result.shared_assessment.username_identity_count == 1);
    CHECK(result.shared_assessment.other_host_identity_count == 0);
    CHECK(result.shared_assessment.schema_grant_count == 1);
    CHECK_FALSE(result.shared_user);
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

TEST_CASE("MariaDBCredentialProvider rejects technically unsupported password values before writing secret files") {
    const std::vector<std::string> invalid_passwords = {
        std::string("nul") + '\0' + "byte",
        std::string("control") + '\x01' + "byte",
        std::string("delete") + '\x7f' + "byte",
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
