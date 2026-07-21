#include "backup/BackupManager.h"
#include "backup/BackupProvider.h"
#include "backup/BackupService.h"
#include "backup/TarBackupProvider.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "doctest/doctest.h"

TEST_CASE("BackupManager create/find/list/remove") {
    containercp::backup::BackupManager mgr;

    uint64_t id = mgr.create(1, 0, "test.tar.gz", 1024, "20240101T000000Z", "/path/test.tar.gz", "gzip");
    CHECK(id == 1);

    auto* b = mgr.find(id);
    REQUIRE(b != nullptr);
    CHECK(b->filename == "test.tar.gz");
    CHECK(b->site_id == 1);
    CHECK(b->size == 1024);
    CHECK(b->file_path == "/path/test.tar.gz");
    CHECK(b->compression == "gzip");

    CHECK(mgr.list().size() == 1);
    CHECK(mgr.remove(id));
    CHECK(mgr.find(id) == nullptr);
}

TEST_CASE("BackupManager find_by_site") {
    containercp::backup::BackupManager mgr;
    mgr.create(1, 0, "a.tar.gz", 100, "t1", "/p/a.tar.gz", "gzip");
    mgr.create(2, 0, "b.tar.gz", 200, "t2", "/p/b.tar.gz", "gzip");
    mgr.create(1, 0, "c.tar.gz", 300, "t3", "/p/c.tar.gz", "gzip");

    auto site1 = mgr.find_by_site(1);
    CHECK(site1.size() == 2);

    auto site2 = mgr.find_by_site(2);
    CHECK(site2.size() == 1);

    auto site3 = mgr.find_by_site(3);
    CHECK(site3.empty());
}

TEST_CASE("BackupManager reserve_id and add_with_id keep ids stable") {
    containercp::backup::BackupManager mgr;

    const auto reserved = mgr.reserve_id();
    CHECK(reserved == 1);

    containercp::backup::Backup b;
    b.id = reserved;
    b.filename = "db5.tar.gz";
    b.site_id = 7;
    b.size = 4096;
    b.created_at = "2026-07-21T00:00:00Z";
    b.file_path = "/tmp/db5.tar.gz";
    b.contains_database = true;
    b.database_status = "included";

    CHECK(mgr.add_with_id(b));
    CHECK_FALSE(mgr.add_with_id(b));
    auto* found = mgr.find(reserved);
    REQUIRE(found != nullptr);
    CHECK(found->id == reserved);
    CHECK(found->name == b.filename);

    CHECK(mgr.create(7, 0, "next.tar.gz", 1, "later", "/tmp/next.tar.gz", "gzip") == 2);
}

TEST_CASE("DB-5 backup manifest JSON round trips safe metadata") {
    containercp::backup::BackupManifest manifest;
    manifest.backup_id = 12;
    manifest.site_id = 34;
    manifest.site_domain = "example.test";
    manifest.created_at = "2026-07-21T12:00:00Z";
    manifest.backup_type = "manual";
    manifest.database_name = "example_db";
    manifest.sql_dump_size = 12345;
    manifest.sql_dump_checksum = "abcdef";
    manifest.archive_checksum = "archive";
    manifest.warnings = {"legacy_backup_database_unknown"};

    const auto json = containercp::backup::backup_manifest_to_json(manifest);
    CHECK(json.find("file_path") == std::string::npos);
    CHECK(json.find("password") == std::string::npos);

    const auto parsed = containercp::backup::backup_manifest_from_json(json);
    REQUIRE(parsed.has_value());
    CHECK(parsed->backup_id == manifest.backup_id);
    CHECK(parsed->site_id == manifest.site_id);
    CHECK(parsed->site_domain == manifest.site_domain);
    CHECK(parsed->database_name == manifest.database_name);
    CHECK(parsed->sql_dump_size == manifest.sql_dump_size);
    CHECK(parsed->sql_dump_checksum == manifest.sql_dump_checksum);
    CHECK(parsed->restore_capability == manifest.restore_capability);
}

TEST_CASE("BackupProvider interface compiles") {
    // This test verifies TarBackupProvider can be constructed
    // and called (without actually running tar)
    // The actual tar execution is tested by the integration tests
    containercp::logger::Logger& log = containercp::logger::Logger::instance();
    containercp::backup::TarBackupProvider provider(log);
    // Just verify the type is correct
    containercp::backup::BackupProvider* bp = &provider;
    CHECK(bp != nullptr);
}

TEST_CASE("TarBackupProvider restores archives containing root entry") {
    namespace fs = std::filesystem;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("containercp-tar-provider-" + std::to_string(::getpid()) + "-" + std::to_string(stamp));
    const auto source = root / "source";
    const auto dest = root / "dest";
    const auto archive = root / "backup.tar.gz";
    fs::create_directories(source / "backup-root");
    {
        std::ofstream manifest(source / "backup-root" / "manifest.json");
        manifest << "{\"schema_version\":\"1\",\"site_id\":1,\"site_domain\":\"example.test\"}\n";
    }

    containercp::logger::Logger& log = containercp::logger::Logger::instance();
    containercp::backup::TarBackupProvider provider(log);
    CHECK(provider.create_backup(source.string(), archive.string()).success);
    CHECK(provider.restore_backup(archive.string(), dest.string()).success);
    CHECK(fs::exists(dest / "backup-root" / "manifest.json"));

    std::error_code ec;
    fs::remove_all(root, ec);
}
