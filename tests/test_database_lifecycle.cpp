#include "doctest/doctest.h"

#include "database/DatabaseIdentifierValidator.h"
#include "database/DatabaseLifecycleAudit.h"
#include "database/DatabaseLifecycleService.h"
#include "database/MariaDBProvider.h"
#include "database/MariaDBSecureTempFile.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace containercp;

namespace {

struct CapturedCommand {
    std::vector<std::string> args;
    std::string stdin_file;
};

class CapturingRunner : public database::MariaDBProcessRunner {
public:
    mutable std::vector<CapturedCommand> commands;
    bool fail_stdin = false;

    runtime::CommandResult run(const std::vector<std::string>& args, const std::string& = "") const override {
        commands.push_back({args, {}});
        return {0, "", ""};
    }

    runtime::CommandResult run_with_stdin_file(const std::vector<std::string>& args,
                                               const std::string& stdin_file,
                                               const std::string& = "") const override {
        commands.push_back({args, stdin_file});
        return fail_stdin ? runtime::CommandResult{1, "", "forced failure"} : runtime::CommandResult{0, "1\n", ""};
    }
};

class FakeProvider : public database::DatabaseProvider {
public:
    bool fail_create_user = false;
    bool fail_grant = false;
    bool fail_login = false;
    bool database_exists_value = false;
    bool user_exists_value = false;
    mutable bool create_database_called = false;
    mutable bool create_user_called = false;
    mutable bool grant_called = false;
    mutable bool drop_database_called = false;
    mutable bool drop_user_called = false;

    database::DatabaseProviderResult ok(std::string code = "ok", std::string output = "1\n") const {
        return {true, std::move(code), "ok", std::move(output)};
    }
    database::DatabaseProviderResult no(std::string code = "missing") const {
        return {false, std::move(code), "missing", {}};
    }

    database::DatabaseProviderResult verify_service_account(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&) const override { return ok("service_account_verified"); }
    database::DatabaseProviderResult database_exists(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { return database_exists_value ? ok("database_exists") : no("database_missing"); }
    database::DatabaseProviderResult user_exists(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { return user_exists_value ? ok("user_exists") : no("user_missing"); }
    database::DatabaseProviderResult user_schema_grant_count(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { return ok("grant_count", "1\n"); }
    database::DatabaseProviderResult create_database(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { create_database_called = true; return ok("database_created"); }
    database::DatabaseProviderResult create_or_update_user(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&, const std::string&) const override { create_user_called = true; return fail_create_user ? no("user_create_failed") : ok("user_created_or_updated"); }
    database::DatabaseProviderResult grant_database_privileges(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&, const std::string&) const override { grant_called = true; return fail_grant ? no("grant_failed") : ok("privileges_granted"); }
    database::DatabaseProviderResult revoke_database_privileges(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&, const std::string&) const override { return ok("privileges_revoked"); }
    database::DatabaseProviderResult drop_database(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { drop_database_called = true; return ok("database_dropped"); }
    database::DatabaseProviderResult drop_user(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { drop_user_called = true; return ok("user_dropped"); }
    database::DatabaseProviderResult verify_login(const database::MariaDBConnectionTarget&, const std::string&, const std::string&, const std::string&) const override { return fail_login ? no("login_failed") : ok("login_verified"); }
};

std::filesystem::path make_site_root(const std::string& domain) {
    auto root = std::filesystem::temp_directory_path() / ("containercp-db3-test-" + std::to_string(::getpid()) + "-" + domain);
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / domain);
    std::ofstream(root / domain / ".env") << "CONTAINERCP_DB_SERVICE_USER=containercp_service\n"
                                          << "CONTAINERCP_DB_SERVICE_PASSWORD=service_secret\n";
    return root;
}

bool argv_contains(const std::vector<std::string>& args, const std::string& needle) {
    for (const auto& arg : args) {
        if (arg.find(needle) != std::string::npos) return true;
    }
    return false;
}

} // namespace

TEST_CASE("MariaDB identifier validation rejects unsafe names") {
    CHECK(database::DatabaseIdentifierValidator::validate_database_name("a").valid);
    CHECK(database::DatabaseIdentifierValidator::validate_database_name(std::string(64, 'a')).valid);
    CHECK_FALSE(database::DatabaseIdentifierValidator::validate_database_name("").valid);
    CHECK_FALSE(database::DatabaseIdentifierValidator::validate_database_name(std::string(65, 'a')).valid);
    CHECK_FALSE(database::DatabaseIdentifierValidator::validate_database_name("1db").valid);
    CHECK_FALSE(database::DatabaseIdentifierValidator::validate_database_name("db-name").valid);
    CHECK_FALSE(database::DatabaseIdentifierValidator::validate_database_name("db name").valid);
    CHECK_FALSE(database::DatabaseIdentifierValidator::validate_database_name("db/name").valid);
    CHECK_FALSE(database::DatabaseIdentifierValidator::validate_database_name("db;DROP").valid);
    CHECK_FALSE(database::DatabaseIdentifierValidator::validate_database_name("db--comment").valid);
    CHECK_FALSE(database::DatabaseIdentifierValidator::validate_user_name("user@host").valid);
    CHECK(database::DatabaseIdentifierValidator::validate_user_name(std::string(32, 'u')).valid);
    CHECK_FALSE(database::DatabaseIdentifierValidator::validate_user_name(std::string(33, 'u')).valid);
}

TEST_CASE("MariaDB secure temp file uses owner-only permissions and cleans up") {
    std::filesystem::path file_path;
    std::filesystem::path dir_path;
    {
        auto temp = database::MariaDBSecureTempFile::create("containercp-db3-perm", ".cnf", "secret");
        file_path = temp.path();
        dir_path = temp.directory();
        struct stat file_stat {};
        REQUIRE(::stat(file_path.c_str(), &file_stat) == 0);
        CHECK((file_stat.st_mode & 0777) == 0600);
        CHECK(database::mariadb_temp_parent_is_safe(dir_path));
    }
    CHECK_FALSE(std::filesystem::exists(file_path));
    CHECK_FALSE(std::filesystem::exists(dir_path));
}

TEST_CASE("MariaDB provider uses argument vectors without password or shell execution") {
    CapturingRunner runner;
    database::MariaDBProvider provider(runner);
    const auto result = provider.create_or_update_user({"/srv/sites/example/docker-compose.yml", "mariadb"},
                                                       {"containercp_service", "admin_secret", "localhost"},
                                                       "app_user",
                                                       "generated_secret");
    CHECK(result.success);
    REQUIRE(runner.commands.size() == 3);
    for (const auto& command : runner.commands) {
        CHECK_FALSE(argv_contains(command.args, "admin_secret"));
        CHECK_FALSE(argv_contains(command.args, "generated_secret"));
        CHECK_FALSE(argv_contains(command.args, "sh"));
        CHECK_FALSE(argv_contains(command.args, "bash"));
        CHECK_FALSE(argv_contains(command.args, "system"));
    }
    CHECK(argv_contains(runner.commands[1].args, "mariadb"));
    CHECK_FALSE(runner.commands[1].stdin_file.empty());
}

TEST_CASE("MariaDB service-account option file escapes option syntax characters") {
    const auto content = database::mariadb_service_account_option_file({"containercp_service", "with space\\slash\nline", "local host"});
    CHECK(content.find("user=containercp_service") != std::string::npos);
    CHECK(content.find("password=with\\sspace\\\\slash\\nline") != std::string::npos);
    CHECK(content.find("host=local\\shost") != std::string::npos);
    CHECK(content.find("with space") == std::string::npos);
}

TEST_CASE("MariaDB provider cleans temporary stdin file when command fails") {
    CapturingRunner runner;
    runner.fail_stdin = true;
    database::MariaDBProvider provider(runner);
    const auto result = provider.create_database({"/srv/sites/example/docker-compose.yml", "mariadb"},
                                                 {"containercp_service", "admin_secret", "localhost"},
                                                 "app_db");
    CHECK_FALSE(result.success);
    REQUIRE(runner.commands.size() == 3);
    CHECK_FALSE(runner.commands[1].stdin_file.empty());
    CHECK_FALSE(std::filesystem::exists(runner.commands[1].stdin_file));
}

TEST_CASE("Database lifecycle create compensates database when user creation fails") {
    site::SiteManager sites;
    database::DatabaseManager databases;
    const auto site_id = sites.create("example.test", "admin", 1);
    const auto database_id = databases.create("example_db", "example_user", "generated_secret", 0, site_id);
    auto root = make_site_root("example.test");
    FakeProvider provider;
    provider.fail_create_user = true;
    int persists = 0;
    database::DatabaseLifecycleService lifecycle(sites, databases, [](const site::Site&) { return "Running"; }, provider, root, [&]() { ++persists; return true; });

    const auto result = lifecycle.createManagedDatabase({site_id, database_id, 7});
    CHECK_FALSE(result.success);
    CHECK(result.code == "user_create_failed");
    CHECK(provider.create_database_called);
    CHECK(provider.create_user_called);
    CHECK(provider.drop_database_called);
    CHECK_FALSE(provider.drop_user_called);
    CHECK(databases.find(database_id) == nullptr);
    CHECK(persists >= 1);
    std::filesystem::remove_all(root);
}

TEST_CASE("Database lifecycle create compensates on grant, verification, and metadata persistence failures") {
    struct Scenario {
        const char* domain;
        bool fail_grant;
        bool fail_login;
        bool fail_persist;
        const char* expected_code;
    };
    const Scenario scenarios[] = {
        {"grant.test", true, false, false, "grant_failed"},
        {"verify.test", false, true, false, "login_failed"},
        {"persist.test", false, false, true, "metadata_persist_failed"},
    };

    for (const auto& scenario : scenarios) {
        site::SiteManager sites;
        database::DatabaseManager databases;
        const auto site_id = sites.create(scenario.domain, "admin", 1);
        const auto database_id = databases.create("app_db", "app_user", "generated_secret", 0, site_id);
        auto root = make_site_root(scenario.domain);
        FakeProvider provider;
        provider.fail_grant = scenario.fail_grant;
        provider.fail_login = scenario.fail_login;
        database::DatabaseLifecycleService lifecycle(sites, databases, [](const site::Site&) { return "Running"; }, provider, root, [&]() { return !scenario.fail_persist; });

        const auto result = lifecycle.createManagedDatabase({site_id, database_id, 9});
        CHECK_FALSE(result.success);
        CHECK(result.code == scenario.expected_code);
        CHECK(provider.drop_database_called);
        CHECK(databases.find(database_id) == nullptr);
        std::filesystem::remove_all(root);
    }
}

TEST_CASE("Database lifecycle rejects imported drop before provider mutation") {
    site::SiteManager sites;
    database::DatabaseManager databases;
    const auto site_id = sites.create("imported.test", "admin", 1);
    const auto database_id = databases.create("imported_db", "imported_user", "", 0, site_id);
    auto root = make_site_root("imported.test");
    FakeProvider provider;
    database::DatabaseLifecycleService lifecycle(sites, databases, [](const site::Site&) { return "Running"; }, provider, root, []() { return true; });

    const auto result = lifecycle.dropManagedDatabase({site_id, database_id, 8, "imported_db", "imported.test", "imported_db"});
    CHECK_FALSE(result.success);
    CHECK(result.code == "ownership_not_managed");
    CHECK_FALSE(provider.drop_database_called);
    CHECK(databases.find(database_id) != nullptr);
    std::filesystem::remove_all(root);
}

TEST_CASE("Database lifecycle drop reconciles already missing physical database by removing metadata") {
    site::SiteManager sites;
    database::DatabaseManager databases;
    const auto site_id = sites.create("missing.test", "admin", 1);
    const auto database_id = databases.create("missing_db", "missing_user", "generated_secret", 0, site_id);
    auto root = make_site_root("missing.test");
    FakeProvider provider;
    provider.database_exists_value = false;
    provider.user_exists_value = false;
    database::DatabaseLifecycleService lifecycle(sites, databases, [](const site::Site&) { return "Running"; }, provider, root, []() { return true; });

    const auto result = lifecycle.dropManagedDatabase({site_id, database_id, 10, "missing_db", "missing.test", "missing_db"});
    CHECK(result.success);
    CHECK(result.code == "already_absent_metadata_removed");
    CHECK_FALSE(provider.drop_database_called);
    CHECK(databases.find(database_id) == nullptr);
    std::filesystem::remove_all(root);
}

TEST_CASE("Database drop confirmation must be exact and non-generic") {
    CHECK(database::database_drop_confirmation_valid("app_db", "app_db", "example.test"));
    CHECK(database::database_drop_confirmation_valid("example.test", "app_db", "example.test"));
    CHECK_FALSE(database::database_drop_confirmation_valid("yes", "app_db", "example.test"));
    CHECK_FALSE(database::database_drop_confirmation_valid("true", "app_db", "example.test"));
    CHECK_FALSE(database::database_drop_confirmation_valid("other_db", "app_db", "example.test"));
}

TEST_CASE("Database lifecycle audit format redacts by omission") {
    const auto line = database::DatabaseLifecycleAuditLogger::format({"drop", "requested", "success", {}, 5, 6, 7, "example.test", false});
    CHECK(line.find("operation=drop") != std::string::npos);
    CHECK(line.find("password") == std::string::npos);
    CHECK(line.find("secret") == std::string::npos);
    CHECK(line.find("option") == std::string::npos);
}
