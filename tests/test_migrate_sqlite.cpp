#include "storage/LegacyArchive.h"
#include "storage/MigrationOrchestrator.h"
#include "storage/Storage.h"

#include <filesystem>
#include <fstream>
#include <string>

#include "doctest/doctest.h"

namespace fs = std::filesystem;
static const char* kFixtureBase = TEST_FIXTURE_DIR "/v0.6.0";

static std::string test_dir(const std::string& name) {
    return (fs::temp_directory_path() / name).string() + "/";
}

static void cleanup(const std::string& dir) {
    fs::remove_all(dir);
}

static void copy_fixtures(const std::string& src_subdir, const std::string& dest) {
    fs::create_directories(dest);
    std::string src = std::string(kFixtureBase) + "/" + src_subdir;
    if (!fs::exists(src)) return;
    for (const auto& entry : fs::directory_iterator(src)) {
        auto path = entry.path();
        if (path.extension() == ".db" || !path.has_extension()) {
            fs::copy(path, fs::path(dest) / path.filename(),
                     fs::copy_options::overwrite_existing);
        }
    }
}

static void assert_no_runtime_txt_fallback_files(const std::string& dir) {
    const char* files[] = {
        "nodes.db", "php_versions.db", "profiles.db", "users.db", "sites.db",
        "domains.db", "databases.db", "backups.db", "reverse_proxies.db",
        "access_users.db", "access_grants.db", "auth_users.db",
        "ssl_certificates.db", "mail_domains.db", "mail_mailboxes.db",
        "mail_aliases.db", "mail_state.db", "mail_smarthost.db", nullptr};
    for (int i = 0; files[i]; ++i) {
        CHECK_FALSE(fs::exists(dir + files[i]));
    }
}

TEST_CASE("MigrationOrchestrator rejects missing source directory") {
    auto tmp = test_dir("mig_reject_source");
    cleanup(tmp);
    fs::create_directories(tmp);

    containercp::storage::MigrationOrchestrator orch(
        "/nonexistent_source", tmp + "containercp.db",
        tmp + "archive", "v0.6.0", "v0.7.0");
    auto r = orch.migrate_to_sqlite();
    CHECK_FALSE(r.success);
    CHECK(r.error.find("Source directory not found") != std::string::npos);

    cleanup(tmp);
}

TEST_CASE("MigrationOrchestrator rejects missing database parent") {
    auto tmp = test_dir("mig_reject_dbparent");
    cleanup(tmp);
    fs::create_directories(tmp);

    std::string src = tmp + "source/";
    copy_fixtures("normal", src);

    containercp::storage::MigrationOrchestrator orch(
        src, "/nonexistent_parent_dir/containercp.db",
        tmp + "archive", "v0.6.0", "v0.7.0");
    auto r = orch.migrate_to_sqlite();
    CHECK_FALSE(r.success);
    CHECK(r.error.find("Database parent directory not found") != std::string::npos);

    cleanup(tmp);
}

TEST_CASE("MigrationOrchestrator rejects invalid source version") {
    auto tmp = test_dir("mig_reject_srcver");
    cleanup(tmp);
    fs::create_directories(tmp);
    std::string src = tmp + "source/";
    copy_fixtures("normal", src);

    containercp::storage::MigrationOrchestrator orch(
        src, tmp + "containercp.db",
        tmp + "archive", "invalid_version", "v0.7.0");
    auto r = orch.migrate_to_sqlite();
    CHECK_FALSE(r.success);
    CHECK(r.error.find("Invalid source version") != std::string::npos);

    cleanup(tmp);
}

TEST_CASE("MigrationOrchestrator happy path") {
    auto tmp = test_dir("mig_happy");
    cleanup(tmp);
    fs::create_directories(tmp);
    std::string src = tmp + "source/";
    std::string db = tmp + "containercp.db";
    std::string archive = tmp + "archive/";
    copy_fixtures("normal", src);

    containercp::storage::MigrationOrchestrator orch(
        src, db, archive, "v0.6.0", "v0.7.0");
    auto r = orch.migrate_to_sqlite();
    REQUIRE(r.success);

    CHECK(containercp::storage::LegacyArchive::valid_migration_id(r.migration_id));
    CHECK(r.verification.success);
    CHECK(r.verification.initial_verification_passed);
    CHECK(r.verification.reopened_verification_passed);
    CHECK(r.archive.success);
    CHECK_FALSE(r.archive.archive_path.empty());
    CHECK(fs::exists(db));

    std::string state_path = containercp::storage::MigrationOrchestrator::activation_state_path(tmp);
    CHECK(fs::exists(state_path));

    cleanup(tmp);
}

TEST_CASE("MigrationOrchestrator activation state content") {
    auto tmp = test_dir("mig_state");
    cleanup(tmp);
    fs::create_directories(tmp);
    std::string src = tmp + "source/";
    std::string db = tmp + "containercp.db";
    std::string archive = tmp + "archive/";
    copy_fixtures("normal", src);

    containercp::storage::MigrationOrchestrator orch(
        src, db, archive, "v0.6.0", "v0.7.0");
    auto r = orch.migrate_to_sqlite();
    REQUIRE(r.success);

    std::string state_path = containercp::storage::MigrationOrchestrator::activation_state_path(tmp);
    REQUIRE(fs::exists(state_path));

    std::ifstream f(state_path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    CHECK(content.find("\"state_version\": 1") != std::string::npos);
    CHECK(content.find("\"active_backend\": \"sqlite\"") != std::string::npos);
    CHECK(content.find(r.migration_id) != std::string::npos);
    CHECK(content.find(db) != std::string::npos);

    cleanup(tmp);
}

TEST_CASE("P11-16 migration diagnostics include operator next steps") {
    auto tmp = test_dir("mig_operator_steps");
    cleanup(tmp);
    fs::create_directories(tmp);
    std::string src = tmp + "source/";
    std::string db = tmp + "containercp.db";
    std::string archive = tmp + "archive/";
    copy_fixtures("normal", src);

    containercp::storage::MigrationOrchestrator orch(
        src, db, archive, "v0.6.0", "v0.7.0");
    auto r = orch.migrate_to_sqlite();
    REQUIRE(r.success);

    CHECK(r.diagnostics.find("Next steps:") != std::string::npos);
    CHECK(r.diagnostics.find("storage.backend = sqlite") != std::string::npos);
    CHECK(r.diagnostics.find("Restart containercpd") != std::string::npos);
    CHECK(r.diagnostics.find("STORAGE startup validation logs pass") != std::string::npos);
    CHECK(r.diagnostics.find(r.archive.archive_path) != std::string::npos);

    cleanup(tmp);
}

TEST_CASE("P11-19 migrated SQLite database opens through production startup gate") {
    auto tmp = test_dir("mig_startup_gate");
    cleanup(tmp);
    fs::create_directories(tmp);
    std::string src = tmp + "source/";
    std::string db = tmp + "containercp.db";
    std::string archive = tmp + "archive/";
    copy_fixtures("normal", src);

    containercp::storage::MigrationOrchestrator orch(
        src, db, archive, "v0.6.0", "v0.7.0");
    auto r = orch.migrate_to_sqlite();
    REQUIRE(r.success);

    containercp::storage::StorageOptions opts;
    opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
    opts.skip_startup_validation = false;
    containercp::storage::Storage storage(tmp, opts);
    REQUIRE(storage.sqlite_ready());

    auto require_snapshot = [](const auto& snap) {
        CHECK(snap.success);
    };
    require_snapshot(storage.load_nodes_checked());
    require_snapshot(storage.load_php_versions_checked());
    require_snapshot(storage.load_profiles_checked());
    require_snapshot(storage.load_users_checked());
    require_snapshot(storage.load_sites_checked());
    require_snapshot(storage.load_domains_checked());
    require_snapshot(storage.load_databases_checked());
    require_snapshot(storage.load_backups_checked());
    require_snapshot(storage.load_reverse_proxies_checked());
    require_snapshot(storage.load_access_users_checked());
    require_snapshot(storage.load_access_grants_checked());
    require_snapshot(storage.load_auth_users_checked());
    require_snapshot(storage.load_ssl_certificates_checked());
    require_snapshot(storage.load_mail_domains_checked());
    require_snapshot(storage.load_mailboxes_checked());
    require_snapshot(storage.load_mail_aliases_checked());

    auto mail_state = storage.load_mail_module_state_checked();
    CHECK(mail_state.success);

    cleanup(tmp);
}

TEST_CASE("P11-R7 production upgrade migrates, activates, writes, restarts, and avoids TXT fallback") {
    auto tmp = test_dir("p11r7_production_upgrade");
    cleanup(tmp);
    fs::create_directories(tmp);
    std::string src = tmp + "source/";
    std::string db = tmp + "containercp.db";
    std::string archive = tmp + "archive/";
    copy_fixtures("normal", src);

    containercp::storage::MigrationOrchestrator orch(
        src, db, archive, "v0.6.0", "v0.7.0");
    auto r = orch.migrate_to_sqlite();
    REQUIRE(r.success);
    CHECK(r.verification.success);
    CHECK(r.verification.initial_verification_passed);
    CHECK(r.verification.reopened_verification_passed);
    CHECK(r.archive.success);
    CHECK(fs::exists(r.archive.archive_path));
    CHECK(fs::exists(containercp::storage::MigrationOrchestrator::activation_state_path(tmp)));
    assert_no_runtime_txt_fallback_files(tmp);

    containercp::storage::StorageOptions opts;
    opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
    opts.skip_startup_validation = false;

    {
        containercp::storage::Storage storage(tmp, opts);
        REQUIRE(storage.sqlite_ready());

        auto migrated_nodes = storage.load_nodes();
        REQUIRE(migrated_nodes.size() == 1);
        CHECK(migrated_nodes[0].id == 1);
        CHECK(migrated_nodes[0].name == "local");

        containercp::node::Node runtime_node;
        runtime_node.id = 2;
        runtime_node.name = "runtime-after-activation";
        runtime_node.type = "local";
        migrated_nodes.push_back(runtime_node);
        storage.save_nodes(migrated_nodes);

        auto written_nodes = storage.load_nodes();
        REQUIRE(written_nodes.size() == 2);
        CHECK(written_nodes[1].id == 2);
        CHECK(written_nodes[1].name == "runtime-after-activation");
        assert_no_runtime_txt_fallback_files(tmp);
    }

    {
        containercp::storage::Storage restarted(tmp, opts);
        REQUIRE(restarted.sqlite_ready());

        auto require_snapshot = [](const auto& snap) {
            CHECK(snap.success);
        };
        require_snapshot(restarted.load_nodes_checked());
        require_snapshot(restarted.load_php_versions_checked());
        require_snapshot(restarted.load_profiles_checked());
        require_snapshot(restarted.load_users_checked());
        require_snapshot(restarted.load_sites_checked());
        require_snapshot(restarted.load_domains_checked());
        require_snapshot(restarted.load_databases_checked());
        require_snapshot(restarted.load_backups_checked());
        require_snapshot(restarted.load_reverse_proxies_checked());
        require_snapshot(restarted.load_access_users_checked());
        require_snapshot(restarted.load_access_grants_checked());
        require_snapshot(restarted.load_auth_users_checked());
        require_snapshot(restarted.load_ssl_certificates_checked());
        require_snapshot(restarted.load_mail_domains_checked());
        require_snapshot(restarted.load_mailboxes_checked());
        require_snapshot(restarted.load_mail_aliases_checked());

        auto restarted_nodes = restarted.load_nodes();
        REQUIRE(restarted_nodes.size() == 2);
        CHECK(restarted_nodes[0].id == 1);
        CHECK(restarted_nodes[1].id == 2);
        CHECK(restarted_nodes[1].name == "runtime-after-activation");

        auto mail_state = restarted.load_mail_module_state_checked();
        CHECK(mail_state.success);
        CHECK(mail_state.present);
        CHECK(mail_state.value == "active");
        assert_no_runtime_txt_fallback_files(tmp);
    }

    cleanup(tmp);
}
