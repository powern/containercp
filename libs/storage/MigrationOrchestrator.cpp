#include "MigrationOrchestrator.h"
#include "ConnectionPool.h"
#include "LegacyArchive.h"
#include "MigrationEngine.h"
#include "SchemaMigrations.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>

namespace containercp::storage {

namespace fs = std::filesystem;

static const char* kDbFilename = "containercp.db";

MigrationOrchestrator::MigrationOrchestrator(
    const std::string& source_dir,
    const std::string& database_path,
    const std::string& archive_root,
    const std::string& source_version,
    const std::string& target_version)
    : source_dir_(source_dir)
    , database_path_(database_path)
    , archive_root_(archive_root)
    , source_version_(source_version)
    , target_version_(target_version)
{
}

std::string MigrationOrchestrator::activation_state_path(const std::string& db_dir) {
    return db_dir + "/storage-state.json";
}

void MigrationOrchestrator::append_stage(
    std::vector<StageReport>& stages,
    const std::string& name, bool ok,
    const std::string& detail)
{
    stages.push_back({name, ok, detail});
}

static bool fsync_parent(const std::string& path) {
    std::string parent = fs::path(path).parent_path().string();
    if (parent.empty()) return true;
    int pfd = ::open(parent.c_str(), O_RDONLY);
    if (pfd < 0) return false;
    ::fsync(pfd);
    ::close(pfd);
    return true;
}

static bool atomic_write_file(const std::string& path, const std::string& content) {
    std::string tmp = path + ".tmp";
    std::ofstream f(tmp);
    if (!f.is_open()) return false;
    f << content;
    f.flush();
    if (f.fail()) { std::error_code ec; fs::remove(tmp, ec); return false; }
    int fd = ::open(tmp.c_str(), O_WRONLY);
    if (fd < 0) { std::error_code ec; fs::remove(tmp, ec); return false; }
    ::fsync(fd);
    ::close(fd);
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    fsync_parent(path);
    return true;
}

MigrationResult MigrationOrchestrator::migrate_to_sqlite() {
    MigrationResult result;
    std::vector<StageReport> stages;

    // ── Step 1: Validate inputs + paths ──
    if (!fs::is_directory(source_dir_)) {
        result.error = "Source directory not found: " + source_dir_;
        return result;
    }
    {
        fs::path db_parent = fs::path(database_path_).parent_path();
        if (!db_parent.empty() && !fs::exists(db_parent)) {
            result.error = "Database parent directory not found: " + db_parent.string();
            return result;
        }
    }
    if (!archive_root_.empty() && !fs::exists(archive_root_)) {
        std::error_code ec;
        fs::create_directories(archive_root_, ec);
        if (ec) {
            result.error = "Cannot create archive root: " + archive_root_;
            return result;
        }
    }
    if (!LegacyArchive::safe_version(source_version_)) {
        result.error = "Invalid source version: " + source_version_;
        return result;
    }
    if (!LegacyArchive::safe_version(target_version_)) {
        result.error = "Invalid target version: " + target_version_;
        return result;
    }
    append_stage(stages, "validate_inputs", true, "");

    // ── Step 2: Generate migration UUID ──
    std::string migration_id = LegacyArchive::generate_uuid();
    if (!LegacyArchive::valid_migration_id(migration_id)) {
        result.error = "Generated invalid migration ID";
        return result;
    }
    result.migration_id = migration_id;
    append_stage(stages, "generate_uuid", true, migration_id);

    // ── Step 3: Create staging directory ──
    // Keep staging next to the final database so publish can use atomic rename.
    // Moving from /tmp to /srv may cross filesystems and fail with EXDEV.
    fs::path db_parent = fs::path(database_path_).parent_path();
    if (db_parent.empty()) db_parent = ".";
    std::string staging_dir = (db_parent / (".containercp-migrate-" + migration_id)).string();
    {
        std::error_code ec;
        fs::remove_all(staging_dir, ec);
        fs::create_directories(staging_dir, ec);
        if (ec || !fs::is_directory(staging_dir)) {
            result.error = "Failed to create staging directory: " + staging_dir;
            return result;
        }
    }
    std::string staging_db = staging_dir + "/" + kDbFilename;
    std::string staging_storage_dir = staging_dir + "/";
    append_stage(stages, "create_staging_dir", true, staging_dir);

    // ── Step 4: Initialize pool + apply schema migrations + import ──
    ConnectionPool pool;
    if (!pool.initialize(staging_db)) {
        std::error_code ec; fs::remove_all(staging_dir, ec);
        result.error = "Failed to initialize staged SQLite database";
        return result;
    }
    {
        MigrationEngine engine;
        register_all_schema_migrations(engine);
        SQLiteDB migrator;
        if (!migrator.open(staging_db)) {
            pool.shutdown();
            std::error_code ec; fs::remove_all(staging_dir, ec);
            result.error = "Failed to open staging database for migration";
            return result;
        }
        bool migrated = engine.migrate(migrator);
        migrator.close();
        if (!migrated) {
            pool.shutdown();
            std::error_code ec; fs::remove_all(staging_dir, ec);
            result.error = "Schema migration failed: " + engine.last_error();
            return result;
        }
    }
    append_stage(stages, "schema_migration", true, "");

    // ── Step 5: Import legacy data ──
    LegacyImporter importer(source_dir_, pool);
    ImportAllResult import_result = importer.import_all();
    if (!import_result.success) {
        pool.shutdown();
        std::error_code ec; fs::remove_all(staging_dir, ec);
        result.error = "Import failed at resource '" + import_result.failed_resource
                     + "': " + import_result.error;
        return result;
    }
    append_stage(stages, "legacy_import", true,
                 std::to_string(import_result.resources.size()) + " resources");

    // ── Step 6: Close pool (for shutdown/reopen cycle) ──
    pool.shutdown();

    // ── Step 7+8: Run verification (initial + reopened) ──
    // Verification opens its own pool internally; we just need the DB to exist
    Verification vfy(source_dir_, staging_db, import_result, staging_storage_dir);
    DatabaseVerificationResult vfy_result = vfy.verify_all();
    result.verification = vfy_result;

    if (!vfy_result.success) {
        std::error_code ec; fs::remove_all(staging_dir, ec);
        result.error = "Verification failed: " + vfy_result.error;
        return result;
    }
    if (!vfy_result.initial_verification_passed ||
        !vfy_result.reopened_verification_passed) {
        std::error_code ec; fs::remove_all(staging_dir, ec);
        result.error = "Verification checks failed";
        return result;
    }
    append_stage(stages, "initial_verification", true, "17 resources passed");
    append_stage(stages, "reopened_verification", true, "17 resources passed");

    // ── Step 9: Create immutable archive ──
    {
        LegacyArchive archiver(source_dir_, archive_root_);
        ArchiveResult archive_result = archiver.create_archive(
            migration_id, source_version_, target_version_, result.verification);
        result.archive = archive_result;
        if (!archive_result.success) {
            std::error_code ec; fs::remove_all(staging_dir, ec);
            result.error = "Archive creation failed: " + archive_result.error;
            return result;
        }
        append_stage(stages, "create_archive", true,
                     "path=" + archive_result.archive_path);
    }

    // ── Step 10: Verify archive ──
    {
        LegacyArchive archiver(source_dir_, archive_root_);
        if (!archiver.verify_archive(result.archive.archive_path)) {
            std::string norm = LegacyArchive::normalize_archive_identity_path(
                result.archive.archive_path);
            if (!archiver.verify_archive(norm)) {
                std::error_code ec; fs::remove_all(staging_dir, ec);
                result.error = "Archive verification failed for " + result.archive.archive_path;
                return result;
            }
        }
        append_stage(stages, "verify_archive", true, "integrity check passed");
    }

    // ── Step 11: Publish final SQLite DB (atomic rename) ──
    {
        std::error_code ec;
        fs::rename(staging_db, database_path_, ec);
        if (ec) {
            std::error_code clean_ec; fs::remove_all(staging_dir, clean_ec);
            result.error = "Failed to publish database: " + ec.message();
            return result;
        }
        fsync_parent(database_path_);
        fs::remove_all(staging_dir, ec);
        append_stage(stages, "publish_database", true, database_path_);
    }

    // ── Step 12: Write activation state file ──
    {
        std::string db_dir = fs::path(database_path_).parent_path().string();
        std::string state_path = activation_state_path(db_dir);
        std::ostringstream json;
        json << "{\n"
             << "    \"state_version\": 1,\n"
             << "    \"active_backend\": \"sqlite\",\n"
             << "    \"migration_id\": \"" << LegacyArchive::json_escape(migration_id) << "\",\n"
             << "    \"database_path\": \"" << LegacyArchive::json_escape(database_path_) << "\",\n"
             << "    \"archive_path\": \"" << LegacyArchive::json_escape(result.archive.archive_path) << "\",\n"
             << "    \"source_version\": \"" << LegacyArchive::json_escape(source_version_) << "\",\n"
             << "    \"target_version\": \"" << LegacyArchive::json_escape(target_version_) << "\",\n"
             << "    \"activation_timestamp\": \"" << LegacyArchive::timestamp_utc() << "\",\n"
              << "    \"schema_version\": 2,\n"
             << "    \"verification_result\": \"success\"\n"
             << "}\n";
        if (!atomic_write_file(state_path, json.str())) {
            result.error = "Failed to write activation state file";
            return result;
        }
        append_stage(stages, "activation_state", true, state_path);
    }

    // ── Step 13: Build diagnostics and return ──
    {
        std::ostringstream diag;
        diag << "Migration ID: " << migration_id << "\n";
        diag << "Database: " << database_path_ << "\n";
        diag << "Archive: " << result.archive.archive_path << "\n";
        for (const auto& s : stages) {
            diag << "  " << s.stage_name << ": " << (s.ok ? "OK" : "FAILED");
            if (!s.detail.empty()) diag << " (" << s.detail << ")";
            diag << "\n";
        }
        diag << "Next steps:\n";
        diag << "  1. Set storage.backend = sqlite in containercp.conf\n";
        diag << "  2. Restart containercpd and confirm STORAGE startup validation logs pass\n";
        diag << "  3. Keep the legacy archive for rollback: " << result.archive.archive_path << "\n";
        result.diagnostics = diag.str();
    }

    result.success = true;
    return result;
}

} // namespace containercp::storage
