#include "backup/BackupManager.h"
#include "backup/BackupProvider.h"
#include "backup/TarBackupProvider.h"

#include <string>

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
