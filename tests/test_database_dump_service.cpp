#include "doctest/doctest.h"

#include "database/DatabaseDumpService.h"
#include "database/MariaDBProvider.h"

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

using namespace containercp;

namespace {

struct DumpFakeProvider : database::DatabaseProvider {
    mutable bool export_called = false;
    mutable bool import_called = false;
    bool fail_import = false;

    database::DatabaseProviderResult ok(std::string code = "ok", std::string output = "1\n") const { return {true, code, "ok", output}; }
    database::DatabaseProviderResult no(std::string code = "failed") const { return {false, code, "safe failure", {}}; }

    database::DatabaseProviderResult verify_service_account(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&) const override { return ok("service_account_verified"); }
    database::DatabaseProviderResult database_exists(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { return ok("database_exists"); }
    database::DatabaseProviderResult user_exists(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { return ok("user_exists"); }
    database::DatabaseProviderResult user_schema_grant_count(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { return ok("grant_count"); }
    database::DatabaseProviderResult create_database(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { return ok("database_created"); }
    database::DatabaseProviderResult create_or_update_user(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&, const std::string&) const override { return ok("user_created_or_updated"); }
    database::DatabaseProviderResult grant_database_privileges(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&, const std::string&) const override { return ok("privileges_granted"); }
    database::DatabaseProviderResult revoke_database_privileges(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&, const std::string&) const override { return ok("privileges_revoked"); }
    database::DatabaseProviderResult drop_database(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { return ok("database_dropped"); }
    database::DatabaseProviderResult drop_user(const database::MariaDBConnectionTarget&, const database::DatabaseProviderCredential&, const std::string&) const override { return ok("user_dropped"); }
    database::DatabaseProviderResult verify_login(const database::MariaDBConnectionTarget&, const std::string&, const std::string&, const std::string&) const override { return ok("login_verified"); }

    database::DatabaseProviderResult export_database(const database::MariaDBConnectionTarget&, const std::string&, const std::string&, const std::string&, const std::string& output_path) const override {
        export_called = true;
        std::ofstream out(output_path);
        out << "-- ContainerCP DB-4 logical export\nCREATE TABLE db4_probe(id int);\nINSERT INTO db4_probe VALUES (1);\n";
        out.close();
        (void)::chmod(output_path.c_str(), S_IRUSR | S_IWUSR);
        return ok("export_completed");
    }

    database::DatabaseProviderResult import_sql_file(const database::MariaDBConnectionTarget&, const std::string&, const std::string&, const std::string&, const std::string&) const override {
        import_called = true;
        return fail_import ? no("mariadb_command_failed") : ok("import_completed");
    }
};

std::filesystem::path make_root(const std::string& name) {
    auto root = std::filesystem::temp_directory_path() / ("containercp-db4-test-" + name + "-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void write_site_env(const std::filesystem::path& root, const std::string& domain) {
    std::filesystem::create_directories(root / domain);
    std::ofstream(root / domain / ".env")
        << "CONTAINERCP_DB_SERVICE_USER=containercp_service\n"
        << "CONTAINERCP_DB_SERVICE_PASSWORD=service-secret\n";
    std::ofstream(root / domain / "docker-compose.yml") << "services:\n  mariadb:\n    image: mariadb:lts\n";
}

bool shell_ok(const std::string& command) {
    return std::system(command.c_str()) == 0;
}

bool docker_mariadb_available() {
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

TEST_CASE("DB-4 artifact IDs are opaque and unpredictable enough for routing") {
    std::set<std::string> ids;
    for (int i = 0; i < 64; ++i) {
        const auto id = database::database_generate_artifact_id();
        CHECK(database::database_artifact_id_valid(id));
        CHECK(ids.insert(id).second);
    }
    CHECK_FALSE(database::database_artifact_id_valid("../escape"));
    CHECK_FALSE(database::database_artifact_id_valid("abc"));
}

TEST_CASE("DB-4 sanitizes filenames and rejects unsafe import content") {
    CHECK(database::database_sanitize_dump_filename("../../secret.sql", "fallback") == "secret.sql");
    CHECK(database::database_sanitize_dump_filename("dump with spaces", "fallback") == "dump_with_spaces.sql");

    auto root = make_root("policy");
    auto good = root / "good.sql";
    std::ofstream(good) << "-- ContainerCP DB-4 logical export\nCREATE TABLE t(id int);\nINSERT INTO t VALUES (1);\n";
    (void)::chmod(good.c_str(), S_IRUSR | S_IWUSR);
    std::string code, message;
    CHECK(database::database_import_content_policy_allows(good, code, message));

    auto bad = root / "bad.sql";
    std::ofstream(bad) << "-- ContainerCP DB-4 logical export\nCREATE USER 'x'@'%' IDENTIFIED BY 'y';\n";
    (void)::chmod(bad.c_str(), S_IRUSR | S_IWUSR);
    CHECK_FALSE(database::database_import_content_policy_allows(bad, code, message));
    CHECK(code == "unsupported_sql_construct");
    std::filesystem::remove_all(root);
}

TEST_CASE("DB-4 export creates path-contained metadata without secrets or paths") {
    site::SiteManager sites;
    database::DatabaseManager databases;
    const uint64_t site_id = sites.create("db4-export.test", "admin", 1);
    const uint64_t database_id = databases.create("db4_export_db", "db4_export_user", "app-secret", 0, site_id);
    auto root = make_root("export");
    write_site_env(root, "db4-export.test");
    DumpFakeProvider provider;
    database::DatabaseDumpService service(sites, databases, [](const site::Site&) { return "Running"; }, provider, root, root / "artifacts");

    const std::string artifact_id = database::database_generate_artifact_id();
    const auto result = service.exportManagedDatabase(database_id, 42, artifact_id);
    CHECK(result.success);
    CHECK(provider.export_called);
    auto meta = service.artifact(database_id, artifact_id);
    REQUIRE(meta.has_value());
    const auto json = database::database_artifact_metadata_json(*meta);
    CHECK(json.find("app-secret") == std::string::npos);
    CHECK(json.find(root.string()) == std::string::npos);
    CHECK(meta->size > 0);
    CHECK(meta->checksum_sha256.size() == 64);
    std::filesystem::remove_all(root);
}

TEST_CASE("DB-4 import requires exact confirmation and reports manual recovery on partial failure") {
    site::SiteManager sites;
    database::DatabaseManager databases;
    const uint64_t site_id = sites.create("db4-import.test", "admin", 1);
    const uint64_t database_id = databases.create("db4_import_db", "db4_import_user", "app-secret", 0, site_id);
    auto root = make_root("import");
    write_site_env(root, "db4-import.test");
    DumpFakeProvider provider;
    database::DatabaseDumpService service(sites, databases, [](const site::Site&) { return "Running"; }, provider, root, root / "artifacts");
    const auto upload = service.stageImportUpload(database_id, "dump.sql", "-- ContainerCP DB-4 logical export\nCREATE TABLE db4_probe(id int);\nINSERT INTO db4_probe VALUES (1);\n");
    REQUIRE(upload.success);

    auto rejected = service.importManagedDatabase(database_id, 43, upload.artifact_id, "wrong");
    CHECK_FALSE(rejected.success);
    CHECK(rejected.code == "confirmation_mismatch");

    provider.fail_import = true;
    auto failed = service.importManagedDatabase(database_id, 44, upload.artifact_id, "db4_import_db");
    CHECK_FALSE(failed.success);
    CHECK(failed.manual_recovery_required);
    CHECK_FALSE(failed.recovery_artifact_id.empty());
    CHECK(provider.import_called);
    std::filesystem::remove_all(root);
}

TEST_CASE("DB-4 real MariaDB export and import restores expected rows in disposable stack") {
    if (!docker_mariadb_available()) {
        MESSAGE("Skipping DB-4 real MariaDB integration; docker compose or cached mariadb:lts image unavailable");
        return;
    }

    const std::string suffix = std::to_string(::getpid());
    const std::string domain = "db4-real-" + suffix + ".test";
    auto root = make_root("real");
    auto site_dir = root / domain;
    auto initdb = site_dir / "initdb";
    std::filesystem::create_directories(initdb);
    std::ofstream(initdb / "10-containercp-service-account.sh")
        << "#!/bin/sh\n"
        << "set -eu\n"
        << "mariadb -uroot -p\"$MYSQL_ROOT_PASSWORD\" <<SQL\n"
        << "CREATE USER IF NOT EXISTS '$CONTAINERCP_DB_SERVICE_USER'@'%' IDENTIFIED BY '$CONTAINERCP_DB_SERVICE_PASSWORD';\n"
        << "GRANT SELECT ON mysql.user TO '$CONTAINERCP_DB_SERVICE_USER'@'%';\n"
        << "GRANT SELECT ON mysql.db TO '$CONTAINERCP_DB_SERVICE_USER'@'%';\n"
        << "GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, INDEX, ALTER, CREATE TEMPORARY TABLES, LOCK TABLES, GRANT OPTION ON \\`db4_real_db\\`.* TO '$CONTAINERCP_DB_SERVICE_USER'@'%';\n"
        << "SQL\n";
    write_site_env(root, domain);
    std::ofstream(site_dir / "docker-compose.yml")
        << "services:\n"
        << "  mariadb:\n"
        << "    image: mariadb:lts\n"
        << "    environment:\n"
        << "      MYSQL_ROOT_PASSWORD: root-secret\n"
        << "      MYSQL_DATABASE: db4_real_db\n"
        << "      MYSQL_USER: db4_real_user\n"
        << "      MYSQL_PASSWORD: app-secret\n"
        << "      CONTAINERCP_DB_SERVICE_USER: containercp_service\n"
        << "      CONTAINERCP_DB_SERVICE_PASSWORD: service-secret\n"
        << "    volumes:\n"
        << "      - ./initdb:/docker-entrypoint-initdb.d:ro\n";
    std::ofstream(site_dir / ".env")
        << "CONTAINERCP_DB_SERVICE_USER=containercp_service\n"
        << "CONTAINERCP_DB_SERVICE_PASSWORD=service-secret\n";

    const std::string compose = (site_dir / "docker-compose.yml").string();
    const std::string down = "docker compose -f " + shell_quote(compose) + " down -v >/dev/null 2>&1";
    REQUIRE(shell_ok("docker compose -f " + shell_quote(compose) + " up -d >/dev/null 2>&1"));
    bool ready = false;
    for (int i = 0; i < 40; ++i) {
        if (shell_ok("docker compose -f " + shell_quote(compose) + " exec -T mariadb mariadb -udb4_real_user -papp-secret -N -B -e 'SELECT 1' db4_real_db >/dev/null 2>&1")) { ready = true; break; }
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
    const auto seed = site_dir / "seed.sql";
    std::ofstream(seed) << "CREATE TABLE db4_probe(id INT PRIMARY KEY, label VARCHAR(32));\nINSERT INTO db4_probe VALUES (7, 'restored');\n";
    (void)::chmod(seed.c_str(), S_IRUSR | S_IWUSR);
    REQUIRE(provider.import_sql_file({compose, "mariadb"}, "db4_real_db", "db4_real_user", "app-secret", seed.string()).success);

    site::SiteManager sites;
    database::DatabaseManager databases;
    const uint64_t site_id = sites.create(domain, "admin", 1);
    const uint64_t database_id = databases.create("db4_real_db", "db4_real_user", "app-secret", 0, site_id);
    database::DatabaseDumpService service(sites, databases, [](const site::Site&) { return "Running"; }, provider, root, root / "artifacts");
    const auto artifact_id = database::database_generate_artifact_id();
    auto exported = service.exportManagedDatabase(database_id, 70, artifact_id);
    CHECK(exported.success);
    auto artifact_path = service.artifact_path(database_id, artifact_id);
    REQUIRE(artifact_path.has_value());
    std::ifstream dump(*artifact_path);
    std::string dump_content((std::istreambuf_iterator<char>(dump)), std::istreambuf_iterator<char>());
    CHECK(dump_content.find("db4_probe") != std::string::npos);
    CHECK(dump_content.find("restored") != std::string::npos);
    CHECK(dump_content.find("app-secret") == std::string::npos);
    CHECK(dump_content.find("mysql.user") == std::string::npos);

    const auto drop = site_dir / "drop.sql";
    std::ofstream(drop) << "DROP TABLE db4_probe;\n";
    (void)::chmod(drop.c_str(), S_IRUSR | S_IWUSR);
    REQUIRE(provider.import_sql_file({compose, "mariadb"}, "db4_real_db", "db4_real_user", "app-secret", drop.string()).success);
    auto imported = service.importManagedDatabase(database_id, 71, artifact_id, "db4_real_db");
    CHECK(imported.success);
    CHECK(shell_ok("docker compose -f " + shell_quote(compose) + " exec -T mariadb mariadb -udb4_real_user -papp-secret -N -B -e 'SELECT COUNT(*) FROM db4_probe WHERE id=7 AND label=\"restored\"' db4_real_db | grep -q '^1$'"));

    CHECK(shell_ok(down));
    std::filesystem::remove_all(root);
}
