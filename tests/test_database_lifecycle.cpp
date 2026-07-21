#include "doctest/doctest.h"

#include "database/DatabaseIdentifierValidator.h"
#include "database/DatabaseLifecycleAudit.h"
#include "database/DatabaseLifecycleService.h"
#include "database/MariaDBProvider.h"
#include "database/MariaDBSecureTempFile.h"
#include "docker/ComposeGenerator.h"
#include "filesystem/Filesystem.h"
#include "logger/Logger.h"
#include "runtime/CommandExecutor.h"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

using namespace containercp;

namespace {

struct CapturedCommand {
    std::vector<std::string> args;
    std::string stdin_file;
    std::string stdin_content;
};

class CapturingRunner : public database::MariaDBProcessRunner {
public:
    mutable std::vector<CapturedCommand> commands;
    bool fail_stdin = false;
    std::string fail_err = "forced failure";

    runtime::CommandResult run(const std::vector<std::string>& args, const std::string& = "") const override {
        commands.push_back({args, {}, {}});
        return {0, "", ""};
    }

    runtime::CommandResult run_with_stdin_file(const std::vector<std::string>& args,
                                               const std::string& stdin_file,
                                               const std::string& = "") const override {
        std::ifstream in(stdin_file);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        commands.push_back({args, stdin_file, content});
        return fail_stdin ? runtime::CommandResult{1, "", fail_err} : runtime::CommandResult{0, "1\n", ""};
    }

    runtime::CommandResult run_stdout_to_file(const std::vector<std::string>& args,
                                              const std::string& output_file,
                                              const std::string& = "") const override {
        commands.push_back({args, output_file, {}});
        std::ofstream out(output_file);
        out << "-- ContainerCP DB-4 logical export\nCREATE TABLE example(id int);\n";
        out.close();
        (void)::chmod(output_file.c_str(), S_IRUSR | S_IWUSR);
        return {0, "", ""};
    }
};

class FakeProvider : public database::DatabaseProvider {
public:
    bool fail_create_user = false;
    bool fail_grant = false;
    bool fail_login = false;
    bool fail_drop_database = false;
    bool fail_drop_user = false;
    bool fail_revoke = false;
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
    database::DatabaseProviderResult revoke_database_privileges(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&, const std::string&) const override { return fail_revoke ? no("revoke_failed") : ok("privileges_revoked"); }
    database::DatabaseProviderResult drop_database(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { drop_database_called = true; return fail_drop_database ? no("drop_database_failed") : ok("database_dropped"); }
    database::DatabaseProviderResult drop_user(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { drop_user_called = true; return fail_drop_user ? no("drop_user_failed") : ok("user_dropped"); }
    database::DatabaseProviderResult verify_login(const database::MariaDBConnectionTarget&, const std::string&, const std::string&, const std::string&) const override { return fail_login ? no("login_failed") : ok("login_verified"); }
    database::DatabaseProviderResult export_database(const database::MariaDBConnectionTarget&, const std::string&, const std::string&, const std::string&, const std::string&) const override { return ok("export_completed"); }
    database::DatabaseProviderResult import_sql_file(const database::MariaDBConnectionTarget&, const std::string&, const std::string&, const std::string&, const std::string&) const override { return ok("import_completed"); }
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

bool shell_ok(const std::string& command) {
    return std::system(command.c_str()) == 0;
}

bool docker_mariadb_integration_available() {
    return shell_ok("docker compose version >/dev/null 2>&1") && shell_ok("docker image inspect mariadb:lts >/dev/null 2>&1");
}

std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out.push_back(c);
    }
    out += "'";
    return out;
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

TEST_CASE("Compose generation passes DB-3 service-account variables into MariaDB") {
    filesystem::Filesystem fs;
    const auto root = std::filesystem::temp_directory_path() / ("containercp-compose-db3-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "templates");
    docker::ComposeGenerator gen(fs, (root / "templates/").string());
    const auto compose_path = root / "docker-compose.yml";
    REQUIRE(gen.generate("test-gui-apache.local", "admin", "php:8.4", compose_path.string(), "12",
                         "httpd:alpine", "/usr/local/apache2/conf/extra", "/usr/local/apache2/logs",
                         "/usr/local/apache2/htdocs", "config/apache", "logs/apache", ""));
    std::ifstream in(compose_path);
    std::string compose((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    CHECK(compose.find("CONTAINERCP_DB_SERVICE_USER=${CONTAINERCP_DB_SERVICE_USER}") != std::string::npos);
    CHECK(compose.find("CONTAINERCP_DB_SERVICE_PASSWORD=${CONTAINERCP_DB_SERVICE_PASSWORD}") != std::string::npos);
    CHECK(compose.find("containercp.site.id=12") != std::string::npos);
    CHECK(compose.find("containercp.domain=test-gui-apache.local") != std::string::npos);
    CHECK(compose.find("CONTAINERCP_DB_SERVICE_PASSWORD=plain") == std::string::npos);
    CHECK(compose.find("MYSQL_ROOT_PASSWORD=plain") == std::string::npos);
    std::filesystem::remove_all(root);
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

TEST_CASE("MariaDB provider grants exact application privileges without FLUSH or global ALL") {
    CapturingRunner runner;
    database::MariaDBProvider provider(runner);
    const auto result = provider.grant_database_privileges({"/srv/sites/example/docker-compose.yml", "mariadb"},
                                                           {"containercp_service", "admin_secret", "localhost"},
                                                           "app_db",
                                                           "app_user");
    CHECK(result.success);
    REQUIRE(runner.commands.size() == 3);
    const auto& sql = runner.commands[1].stdin_content;
    CHECK(sql.find("GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, INDEX, ALTER, CREATE TEMPORARY TABLES, LOCK TABLES ON `app_db`.* TO 'app_user'@'%'") != std::string::npos);
    CHECK(sql.find("FLUSH PRIVILEGES") == std::string::npos);
    CHECK(sql.find("ALL PRIVILEGES ON *.*") == std::string::npos);
    CHECK(sql.find("WITH GRANT OPTION") == std::string::npos);
}

TEST_CASE("MariaDB provider revokes exact application privileges without FLUSH") {
    CapturingRunner runner;
    database::MariaDBProvider provider(runner);
    const auto result = provider.revoke_database_privileges({"/srv/sites/example/docker-compose.yml", "mariadb"},
                                                            {"containercp_service", "admin_secret", "localhost"},
                                                            "app_db",
                                                            "app_user");
    CHECK(result.success);
    REQUIRE(runner.commands.size() == 3);
    const auto& sql = runner.commands[1].stdin_content;
    CHECK(sql.find("REVOKE ALL PRIVILEGES ON `app_db`.* FROM 'app_user'@'%'") != std::string::npos);
    CHECK(sql.find("FLUSH PRIVILEGES") == std::string::npos);
}

TEST_CASE("MariaDB provider classifies missing RELOAD privilege and redacts SQL passwords") {
    CapturingRunner runner;
    runner.fail_stdin = true;
    runner.fail_err = "--------------\nALTER USER 'app_user'@'%' IDENTIFIED BY 'generated_secret'\n--------------\nERROR 1227 (42000): Access denied; you need (at least one of) the RELOAD privilege(s) for this operation";
    database::MariaDBProvider provider(runner);
    const auto result = provider.create_or_update_user({"/srv/sites/example/docker-compose.yml", "mariadb"},
                                                       {"containercp_service", "admin_secret", "localhost"},
                                                       "app_user",
                                                       "generated_secret");
    CHECK_FALSE(result.success);
    CHECK(result.code == "mariadb_reload_privilege_required");
    CHECK(result.message.find("generated_secret") == std::string::npos);
    CHECK(result.message.find("<redacted>") != std::string::npos);
}

TEST_CASE("MariaDB provider classifies GRANT privilege denial") {
    CapturingRunner runner;
    runner.fail_stdin = true;
    runner.fail_err = "ERROR 1044 (42000): Access denied for user 'containercp_service'@'%' to database 'app_db' while running GRANT";
    database::MariaDBProvider provider(runner);
    const auto result = provider.grant_database_privileges({"/srv/sites/example/docker-compose.yml", "mariadb"},
                                                           {"containercp_service", "admin_secret", "localhost"},
                                                           "app_db",
                                                           "app_user");
    CHECK_FALSE(result.success);
    CHECK(result.code == "mariadb_grant_privilege_denied");
    CHECK(result.message.find("admin_secret") == std::string::npos);
}

TEST_CASE("MariaDB provider export/import command vectors avoid shell and password argv") {
    CapturingRunner runner;
    database::MariaDBProvider provider(runner);
    const auto root = make_site_root("provider-export.test");
    const auto output = root / "dump.sql";
    const auto export_result = provider.export_database({"/srv/sites/example/docker-compose.yml", "mariadb"},
                                                        "app_db",
                                                        "app_user",
                                                        "generated_secret",
                                                        output.string());
    CHECK(export_result.success);
    REQUIRE(runner.commands.size() >= 3);
    const auto& dump_args = runner.commands[1].args;
    CHECK(argv_contains(dump_args, "mariadb-dump"));
    CHECK(argv_contains(dump_args, "--single-transaction"));
    CHECK(argv_contains(dump_args, "--quick"));
    CHECK(argv_contains(dump_args, "--skip-lock-tables"));
    CHECK(argv_contains(dump_args, "--hex-blob"));
    CHECK(argv_contains(dump_args, "--default-character-set=utf8mb4"));
    CHECK(argv_contains(dump_args, "app_db"));
    for (const auto& arg : dump_args) CHECK(arg.find("generated_secret") == std::string::npos);

    runner.commands.clear();
    const auto import_result = provider.import_sql_file({"/srv/sites/example/docker-compose.yml", "mariadb"},
                                                        "app_db",
                                                        "app_user",
                                                        "generated_secret",
                                                        output.string());
    CHECK(import_result.success);
    REQUIRE(runner.commands.size() >= 3);
    const auto& import_args = runner.commands[1].args;
    CHECK(argv_contains(import_args, "mariadb"));
    CHECK(argv_contains(import_args, "--database"));
    CHECK(argv_contains(import_args, "app_db"));
    CHECK(argv_contains(import_args, "--local-infile=0"));
    for (const auto& arg : import_args) CHECK(arg.find("generated_secret") == std::string::npos);
    std::filesystem::remove_all(root);
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

TEST_CASE("Database lifecycle create reports manual recovery when grant-stage compensation fails") {
    site::SiteManager sites;
    database::DatabaseManager databases;
    const auto site_id = sites.create("cleanup-failure.test", "admin", 1);
    const auto database_id = databases.create("app_db", "app_user", "generated_secret", 0, site_id);
    auto root = make_site_root("cleanup-failure.test");
    FakeProvider provider;
    provider.fail_grant = true;
    provider.fail_drop_user = true;
    database::DatabaseLifecycleService lifecycle(sites, databases, [](const site::Site&) { return "Running"; }, provider, root, []() { return true; });

    const auto result = lifecycle.createManagedDatabase({site_id, database_id, 12});
    CHECK_FALSE(result.success);
    CHECK(result.manual_recovery_required);
    CHECK(result.code == "drop_user_failed");
    CHECK(result.failure.step_name == "Compensating changes");
    CHECK(result.failure.manual_recovery_required);
    CHECK(provider.drop_database_called);
    CHECK(databases.find(database_id) == nullptr);
    std::filesystem::remove_all(root);
}

TEST_CASE("MariaDB provider real disposable drop and recreate cycle uses scoped grants without root fallback") {
    if (!docker_mariadb_integration_available()) {
        MESSAGE("Skipping real MariaDB lifecycle integration; docker compose or cached mariadb:lts image unavailable");
        return;
    }

    const std::string suffix = std::to_string(::getpid());
    const auto root = std::filesystem::temp_directory_path() / ("containercp-db3-real-" + suffix);
    const auto compose = root / "docker-compose.yml";
    const auto initdb = root / "initdb";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(initdb);
    std::ofstream(initdb / "10-containercp-service-account.sh")
        << "#!/bin/sh\n"
        << "set -eu\n"
        << "mariadb -uroot -p\"$MYSQL_ROOT_PASSWORD\" <<SQL\n"
        << "CREATE USER IF NOT EXISTS '$CONTAINERCP_DB_SERVICE_USER'@'%' IDENTIFIED BY '$CONTAINERCP_DB_SERVICE_PASSWORD';\n"
        << "GRANT CREATE ON *.* TO '$CONTAINERCP_DB_SERVICE_USER'@'%';\n"
        << "GRANT CREATE USER ON *.* TO '$CONTAINERCP_DB_SERVICE_USER'@'%';\n"
        << "GRANT SELECT ON mysql.user TO '$CONTAINERCP_DB_SERVICE_USER'@'%';\n"
        << "GRANT SELECT ON mysql.db TO '$CONTAINERCP_DB_SERVICE_USER'@'%';\n"
        << "GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, INDEX, ALTER, CREATE TEMPORARY TABLES, LOCK TABLES, GRANT OPTION ON \\`test_gui_apache_local_db\\`.* TO '$CONTAINERCP_DB_SERVICE_USER'@'%';\n"
        << "SQL\n";
    std::ofstream(compose)
        << "services:\n"
        << "  mariadb:\n"
        << "    image: mariadb:lts\n"
        << "    environment:\n"
        << "      MYSQL_ROOT_PASSWORD: root-secret\n"
        << "      MYSQL_DATABASE: test_gui_apache_local_db\n"
        << "      MYSQL_USER: test_gui_apache_local_user\n"
        << "      MYSQL_PASSWORD: app-secret\n"
        << "      CONTAINERCP_DB_SERVICE_USER: containercp_service\n"
        << "      CONTAINERCP_DB_SERVICE_PASSWORD: service-secret\n"
        << "    volumes:\n"
        << "      - ./initdb:/docker-entrypoint-initdb.d:ro\n";

    const std::string compose_s = compose.string();
    const std::string down = "docker compose -f " + shell_quote(compose_s) + " down -v >/dev/null 2>&1";
    REQUIRE(shell_ok("docker compose -f " + shell_quote(compose_s) + " up -d >/dev/null 2>&1"));
    bool ready = false;
    for (int i = 0; i < 40; ++i) {
        if (shell_ok("docker compose -f " + shell_quote(compose_s) + " exec -T mariadb sh -lc 'mariadb -u\"$CONTAINERCP_DB_SERVICE_USER\" -p\"$CONTAINERCP_DB_SERVICE_PASSWORD\" -N -B -e \"SELECT 1\" >/dev/null 2>&1'")) {
            ready = true;
            break;
        }
        (void)std::system("sleep 1");
    }
    if (!ready) {
        (void)shell_ok(down);
        std::filesystem::remove_all(root);
    }
    REQUIRE(ready);

    runtime::CommandExecutor executor;
    database::MariaDBCommandExecutorRunner runner(executor);
    database::MariaDBProvider provider(runner);
    const database::MariaDBConnectionTarget target{compose_s, "mariadb"};
    const database::DatabaseProviderCredential service{"containercp_service", "service-secret", "localhost"};

    CHECK(provider.verify_service_account(target, service).success);
    CHECK(provider.drop_database(target, service, "test_gui_apache_local_db").success);
    CHECK(provider.drop_user(target, service, "test_gui_apache_local_user").success);
    CHECK_FALSE(provider.database_exists(target, service, "test_gui_apache_local_db").success);
    CHECK_FALSE(provider.user_exists(target, service, "test_gui_apache_local_user").success);
    CHECK(provider.create_database(target, service, "test_gui_apache_local_db").success);
    CHECK(provider.create_or_update_user(target, service, "test_gui_apache_local_user", "app-secret-recreated").success);
    CHECK(provider.grant_database_privileges(target, service, "test_gui_apache_local_db", "test_gui_apache_local_user").success);
    CHECK(provider.verify_login(target, "test_gui_apache_local_db", "test_gui_apache_local_user", "app-secret-recreated").success);

    const auto grant_count = provider.user_schema_grant_count(target, service, "test_gui_apache_local_user");
    CHECK(grant_count.success);
    CHECK(grant_count.output.find("1") != std::string::npos);
    const auto service_grants = provider.verify_service_account(target, service);
    CHECK(service_grants.success);

    const auto app_global = executor.run({"docker", "compose", "-f", compose_s, "exec", "-T", "mariadb", "sh", "-lc",
        "mariadb -u\"$CONTAINERCP_DB_SERVICE_USER\" -p\"$CONTAINERCP_DB_SERVICE_PASSWORD\" -N -B -e \"SELECT COUNT(*) FROM mysql.user WHERE User='test_gui_apache_local_user' AND Host='%' AND (Select_priv='Y' OR Insert_priv='Y' OR Update_priv='Y' OR Delete_priv='Y' OR Create_priv='Y' OR Drop_priv='Y' OR Reload_priv='Y' OR Grant_priv='Y' OR Super_priv='Y' OR Alter_priv='Y')\""});
    CHECK(app_global.exit_code == 0);
    CHECK(app_global.out.find("0") != std::string::npos);

    const auto service_grant_text = executor.run({"docker", "compose", "-f", compose_s, "exec", "-T", "mariadb", "sh", "-lc",
        "mariadb -u\"$CONTAINERCP_DB_SERVICE_USER\" -p\"$CONTAINERCP_DB_SERVICE_PASSWORD\" -N -B -e \"SHOW GRANTS FOR CURRENT_USER()\""});
    CHECK(service_grant_text.exit_code == 0);
    CHECK(service_grant_text.out.find("GRANT ALL PRIVILEGES ON *.*") == std::string::npos);
    CHECK(service_grant_text.out.find("RELOAD") == std::string::npos);

    CHECK(shell_ok(down));
    std::filesystem::remove_all(root);
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
