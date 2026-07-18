#include "storage/LegacyArchive.h"
#include "storage/MigrationOrchestrator.h"

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
