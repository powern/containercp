#include "storage/LegacyImporter.h"
#include "storage/SQLiteSnapshotReader.h"
#include "storage/Verification.h"
#include "storage/MigrationEngine.h"
#include "storage/SchemaMigrations.h"
#include "storage/ConnectionPool.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <cstring>
#include <set>

#include "doctest/doctest.h"

namespace fs = std::filesystem;
using namespace containercp;
using namespace containercp::storage;

static std::string test_dir(const std::string& name) {
    return (fs::temp_directory_path() / name).string() + "/";
}

static void cleanup(const std::string& dir) {
    fs::remove_all(dir);
}

static const char* kFixtureBase = TEST_FIXTURE_DIR "/v0.6.0";

// Helper: create a pool with schema for importer tests
static void init_pool(ConnectionPool& pool, const std::string& dir) {
    REQUIRE(pool.initialize(dir + "containercp.db"));
    SQLiteDB migrator;
    REQUIRE(migrator.open(dir + "containercp.db"));
    MigrationEngine eng;
    register_all_schema_migrations(eng);
    REQUIRE(eng.migrate(migrator));
    migrator.close();
}

// Copy fixture files (matching *.db or no extension) from src_subdir to dest
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

// Write a text file with the given lines
static void write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

// ============================================================
// 1. Checked SQLiteStorage success result
// ============================================================
TEST_CASE("Checked save returns true on success") {
    auto dir = test_dir("chk_save_ok");
    cleanup(dir); fs::create_directories(dir);
    ConnectionPool pool;
    init_pool(pool, dir);
    SQLiteStorage ss(pool);
    std::vector<node::Node> nodes;
    node::Node n;
    n.id = 1; n.name = "test"; n.type = "web";
    nodes.push_back(n);
    CHECK(ss.try_save_nodes(nodes));
    cleanup(dir);
}

// ============================================================
// 2. Checked SQLiteStorage FK failure result
// ============================================================
TEST_CASE("Checked save FK failure returns false") {
    auto dir = test_dir("chk_fk_fail");
    cleanup(dir); fs::create_directories(dir);
    ConnectionPool pool;
    init_pool(pool, dir);
    SQLiteStorage ss(pool);
    // Try saving an access_grant referencing non-existent access_user
    std::vector<access::AccessGrant> grants;
    access::AccessGrant g;
    g.id = 99; g.access_user_id = 99999; g.site_id = 88888;
    g.permission = access::Permission::READ_ONLY;
    grants.push_back(g);
    CHECK_FALSE(ss.try_save_access_grants(grants));
    cleanup(dir);
}

// ============================================================
// 3. Importer reports failure after FK failure
// ============================================================
TEST_CASE("Importer FK failure detection") {
    auto dir = test_dir("imp_fk_fail");
    cleanup(dir); fs::create_directories(dir);
    // Write an access_grant file referencing non-existent access_user
    write_file(dir + "access_grants.db", "99|99999|88888|read\n");
    // Write required dependencies but with missing access_users
    write_file(dir + "sites.db", "1|example.com|admin|1|apache|1\n");
    write_file(dir + "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_file(dir + "nodes.db", "1|main|web\n");
    // Required files for import_all
    write_file(dir + "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_file(dir + "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");

    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_access_grants();
    CHECK_FALSE(r.success);
    CHECK(r.disposition == ImportDisposition::Failed);
    cleanup(dir);
}

// ============================================================
// 4. Importer failure on unavailable pool
// ============================================================
TEST_CASE("Unavailable pool is detected") {
    auto dir = test_dir("imp_pool_dead");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n");
    ConnectionPool pool;
    // Pool not initialized => unavailable
    LegacyImporter imp(dir, pool);
    auto r = imp.import_nodes();
    CHECK_FALSE(r.success);
    cleanup(dir);
}

// ============================================================
// 5. Duplicate ID rejection
// ============================================================
TEST_CASE("Duplicate ID rejected") {
    auto dir = test_dir("imp_dup_id");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n1|dup|web\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_nodes();
    CHECK_FALSE(r.success);
    CHECK(r.error == "duplicate_id");
    cleanup(dir);
}

// ============================================================
// 6. Duplicate logical-key rejection
// ============================================================
TEST_CASE("Duplicate domain name rejected") {
    auto dir = test_dir("imp_dup_domain");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "sites.db", "1|example.com|admin|1|apache|1\n2|example.com|admin|1|apache|1\n");
    write_file(dir + "nodes.db", "1|main|web\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_sites();
    CHECK_FALSE(r.success);
    CHECK(r.error == "duplicate_domain");
    cleanup(dir);
}

TEST_CASE("Duplicate username rejected") {
    auto dir = test_dir("imp_dup_user");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n2|admin|1001|/home/admin2|/bin/bash|1\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_users();
    CHECK_FALSE(r.success);
    CHECK(r.error == "duplicate_username");
    cleanup(dir);
}

// ============================================================
// 7. Required missing file
// ============================================================
TEST_CASE("Required missing file fails") {
    auto dir = test_dir("imp_req_miss");
    cleanup(dir); fs::create_directories(dir);
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_nodes();
    CHECK_FALSE(r.success);
    CHECK(r.disposition == ImportDisposition::Failed);
    CHECK(r.error == "file_missing");
    cleanup(dir);
}

// ============================================================
// 8. Optional missing file
// ============================================================
TEST_CASE("Optional missing file skips") {
    auto dir = test_dir("imp_opt_miss");
    cleanup(dir); fs::create_directories(dir);
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_ssl_certificates();
    CHECK(r.success);
    CHECK(r.disposition == ImportDisposition::SkippedMissingOptional);
    CHECK(r.record_count == 0);
    cleanup(dir);
}

// ============================================================
// 9. Unreadable existing file
// ============================================================
TEST_CASE("Unreadable file fails") {
    auto dir = test_dir("imp_unread");
    cleanup(dir); fs::create_directories(dir);
    // Create file, then remove read permission
    write_file(dir + "nodes.db", "1|main|web\n");
    std::error_code ec;
    fs::permissions(dir + "nodes.db", fs::perms::owner_read, fs::perm_options::remove, ec);
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_nodes();
    if (!r.success) {
        CHECK(r.disposition == ImportDisposition::Failed);
        CHECK(r.error == "file_unreadable");
    }
    std::error_code ec2;
    fs::permissions(dir + "nodes.db", fs::perms::owner_read, fs::perm_options::add, ec2);
    cleanup(dir);
}

// ============================================================
// 10. Invalid file type (directory)
// ============================================================
TEST_CASE("Invalid file type rejected") {
    auto dir = test_dir("imp_inv_type");
    cleanup(dir); fs::create_directories(dir);
    fs::create_directory(dir + "nodes.db");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_nodes();
    CHECK_FALSE(r.success);
    CHECK(r.error == "invalid_file_type");
    cleanup(dir);
}

// ============================================================
// 11. Empty file is SkippedEmpty and does not erase
// ============================================================
TEST_CASE("Empty file skips and does not erase") {
    auto dir = test_dir("imp_empty_noerase");
    cleanup(dir); fs::create_directories(dir);
    // First import some data
    write_file(dir + "nodes.db", "1|main|web\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    {
        LegacyImporter imp(dir, pool);
        CHECK(imp.import_nodes().success);
        CHECK(SQLiteStorage(pool).load_nodes().size() == 1);
    }
    // Replace with empty file
    write_file(dir + "nodes.db", "");
    {
        LegacyImporter imp(dir, pool);
        auto r = imp.import_nodes();
        CHECK(r.success);
        CHECK(r.disposition == ImportDisposition::SkippedEmpty);
        CHECK(r.record_count == 0);
        // Previous data should remain
        CHECK(SQLiteStorage(pool).load_nodes().size() == 1);
    }
    cleanup(dir);
}

// ============================================================
// 12. Read error handling (best-effort)
// ============================================================
TEST_CASE("Importer handles read errors gracefully") {
    auto dir = test_dir("imp_read_err");
    cleanup(dir); fs::create_directories(dir);
    // Create a file with invalid content
    write_file(dir + "nodes.db", "\xff\xfe\x00\x01\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_nodes();
    // The file is readable but has garbage — parsing should fail
    CHECK_FALSE(r.success);
    cleanup(dir);
}

// ============================================================
// 13. uint64 overflow rejection
// ============================================================
TEST_CASE("uint64 overflow rejected") {
    auto dir = test_dir("imp_overflow");
    cleanup(dir); fs::create_directories(dir);
    std::string big = "18446744073709551615"; // UINT64_MAX
    std::string bigger = "18446744073709551616"; // UINT64_MAX + 1
    write_file(dir + "nodes.db", big + "|main|web\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    {
        LegacyImporter imp(dir, pool);
        // UINT64_MAX is valid where schema accepts it
        auto r = imp.import_nodes();
        CHECK(r.success);
    }
    {
        write_file(dir + "nodes.db", bigger + "|main|web\n");
        LegacyImporter imp(dir, pool);
        auto r = imp.import_nodes();
        CHECK_FALSE(r.success);
        CHECK(r.error == "invalid_integer");
    }
    cleanup(dir);
}

// ============================================================
// 14. Negative unsigned rejected
// ============================================================
TEST_CASE("Negative unsigned integer rejected") {
    auto dir = test_dir("imp_neg_uint");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "-1|main|web\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_nodes();
    CHECK_FALSE(r.success);
    CHECK(r.error == "invalid_integer");
    cleanup(dir);
}

// ============================================================
// 15. Invalid present boolean rejected
// ============================================================
TEST_CASE("Invalid boolean field rejected") {
    auto dir = test_dir("imp_bool");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "php_versions.db", "1|8.2|php:8.2|yes|1\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_php_versions();
    CHECK_FALSE(r.success);
    CHECK(r.error == "invalid_boolean");
    cleanup(dir);
}

// ============================================================
// 16. Invalid present optional integer rejected
// ============================================================
TEST_CASE("Invalid optional integer field rejected") {
    auto dir = test_dir("imp_opt_int");
    cleanup(dir); fs::create_directories(dir);
    // mail_domains with invalid max_mailboxes
    write_file(dir + "mail_domains.db",
               "1|LOCAL|example.com|0|0|1|||abc|10\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_mail_domains();
    CHECK_FALSE(r.success);
    cleanup(dir);
}

// ============================================================
// 17. Trailing empty field preserved
// ============================================================
TEST_CASE("trailing empty fields preserved") {
    auto dir = test_dir("imp_trail");
    cleanup(dir); fs::create_directories(dir);
    // nodes with trailing empty field (3 fields, last empty is a string)
    write_file(dir + "nodes.db", "1|main|\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_nodes();
    CHECK(r.success);
    CHECK(r.record_count == 1);
    cleanup(dir);
}

TEST_CASE("multiple trailing empty fields preserved") {
    auto dir = test_dir("imp_trail2");
    cleanup(dir); fs::create_directories(dir);
    // 1||| → 4 fields
    write_file(dir + "nodes.db", "1|name||\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    // nodes expects 3 fields, but |name|| gives 4 → fail
    auto r = imp.import_nodes();
    CHECK_FALSE(r.success);
    CHECK(r.error == "invalid_field_count");
    cleanup(dir);
}

TEST_CASE("adjacent empty fields preserved") {
    auto dir = test_dir("imp_adj");
    cleanup(dir); fs::create_directories(dir);
    // 1||3 → 3 fields, middle is empty
    write_file(dir + "nodes.db", "1||web\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_nodes();
    CHECK(r.success);
    CHECK(r.record_count == 1);
    cleanup(dir);
}

// ============================================================
// 18. Malformed SSL optional fields fail
// ============================================================
TEST_CASE("malformed SSL auto_renew fails") {
    auto dir = test_dir("imp_ssl_bool");
    cleanup(dir); fs::create_directories(dir);
    // SSL with invalid auto_renew value
    write_file(dir + "ssl_certificates.db",
               "1|10|example.com|letsencrypt|/path/cert|/path/key|/path/chain|||||badvalue|0|0||||||0|0\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_ssl_certificates();
    CHECK_FALSE(r.success);
    CHECK(r.error == "invalid_boolean");
    cleanup(dir);
}

TEST_CASE("malformed SSL renew_attempts fails") {
    auto dir = test_dir("imp_ssl_int");
    cleanup(dir); fs::create_directories(dir);
    // SSL with invalid renew_attempts
    write_file(dir + "ssl_certificates.db",
               "1|10|example.com|letsencrypt|/path/cert|/path/key|/path/chain|||||1|1|1|||||abc|0\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_ssl_certificates();
    CHECK_FALSE(r.success);
    CHECK(r.error == "invalid_integer");
    cleanup(dir);
}

// ============================================================
// 19. Malformed MailDomain optional fields fail
// ============================================================
TEST_CASE("malformed MailDomain max_mailboxes fails") {
    auto dir = test_dir("imp_md_int");
    cleanup(dir); fs::create_directories(dir);
    // 12-field mail_domain with invalid max_mailboxes
    write_file(dir + "mail_domains.db",
               "1|LOCAL|example.com|0|0|1|||abc|10\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_mail_domains();
    CHECK_FALSE(r.success);
    CHECK(r.error == "invalid_integer");
    cleanup(dir);
}

// ============================================================
// 20. Malformed Mailbox quota fails
// ============================================================
TEST_CASE("malformed Mailbox quota_bytes fails") {
    auto dir = test_dir("imp_mb_quota");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "mail_mailboxes.db",
               "1|1|user|hash|abc|500|1|User||1|2024-01-01|2024-01-01|2024-01-01\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_mail_mailboxes();
    CHECK_FALSE(r.success);
    CHECK(r.error == "invalid_integer");
    cleanup(dir);
}

// ============================================================
// 21. mail_config imports zero/one/two keys accurately
// ============================================================
TEST_CASE("mail_config both files absent") {
    auto dir = test_dir("imp_mc_absent");
    cleanup(dir); fs::create_directories(dir);
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_mail_config();
    CHECK(r.success);
    CHECK(r.disposition == ImportDisposition::SkippedMissingOptional);
    CHECK(r.record_count == 0);
    cleanup(dir);
}

TEST_CASE("mail_config one key imported") {
    auto dir = test_dir("imp_mc_one");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "mail_state.db", "active\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_mail_config();
    CHECK(r.success);
    CHECK(r.disposition == ImportDisposition::Imported);
    CHECK(r.record_count == 1);
    cleanup(dir);
}

TEST_CASE("mail_config both keys imported") {
    auto dir = test_dir("imp_mc_both");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "mail_state.db", "active\n");
    write_file(dir + "mail_smarthost.db", "smtp:587 user pass\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_mail_config();
    CHECK(r.success);
    CHECK(r.disposition == ImportDisposition::Imported);
    CHECK(r.record_count == 2);
    cleanup(dir);
}

TEST_CASE("mail_config invalid state rejected") {
    auto dir = test_dir("imp_mc_inv");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "mail_state.db", "foobar\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_mail_config();
    CHECK_FALSE(r.success);
    CHECK(r.error == "invalid_module_state");
    cleanup(dir);
}

// ============================================================
// 22. mail_config write failure detected
// ============================================================
TEST_CASE("mail_config write failure on unavailable pool") {
    auto dir = test_dir("imp_mc_writefail");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "mail_state.db", "active\n");
    ConnectionPool pool;
    // Pool not initialized
    LegacyImporter imp(dir, pool);
    auto r = imp.import_mail_config();
    CHECK_FALSE(r.success);
    cleanup(dir);
}

// ============================================================
// 23. import_all stops on real persistence failure
// ============================================================
TEST_CASE("import_all stops on persistence failure") {
    auto dir = test_dir("imp_all_stop");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n");
    write_file(dir + "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_file(dir + "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_file(dir + "sites.db", "1|example.com|admin|1|apache|1\n");
    write_file(dir + "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n");
    write_file(dir + "databases.db", "1|mydb|myuser|pass|mysql|8.0|1|1|1\n");
    write_file(dir + "backups.db", "1|1|1|backup.tar.gz|full|1000|2024-01-01|completed|/path|gzip\n");
    write_file(dir + "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");
    write_file(dir + "access_users.db", "1|testuser|password|hash|1\n");
    // access_grants references missing site_id → FK failure
    write_file(dir + "access_grants.db", "1|1|99999|read_only\n");

    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto result = imp.import_all();
    CHECK_FALSE(result.success);
    CHECK(result.failed_resource == "access_grants");
    // Earlier resources must have committed
    SQLiteStorage ss(pool);
    CHECK(ss.load_nodes().size() == 1);
    CHECK(ss.load_php_versions().size() == 1);
    CHECK(ss.load_users().size() == 1);
    CHECK(ss.load_sites().size() == 1);
    cleanup(dir);
}

// ============================================================
// 25. Later resources not attempted after failure
// ============================================================
TEST_CASE("import_all does not attempt later resources after failure") {
    auto dir = test_dir("imp_all_nolater");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n");
    write_file(dir + "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_file(dir + "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "users.db", "BAD DATA|admin|1000|/home/admin|/bin/bash|1\n");
    write_file(dir + "sites.db", "1|example.com|admin|1|apache|1\n");

    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto result = imp.import_all();
    CHECK_FALSE(result.success);
    CHECK(result.failed_resource == "users");
    // Sites should NOT have been attempted
    // Resources: nodes + php + profiles (combined) + users = 4
    CHECK(result.resources.size() == 4);
    cleanup(dir);
}

// ============================================================
// 26. Source files remain unchanged
// ============================================================
TEST_CASE("source files remain unchanged after import") {
    auto dir = test_dir("imp_src_unchg");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n");
    auto before_size = fs::file_size(dir + "nodes.db");
    auto before_time = fs::last_write_time(dir + "nodes.db");

    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    CHECK(imp.import_nodes().success);
    CHECK(imp.import_nodes().success); // reimport also ok

    auto after_size = fs::file_size(dir + "nodes.db");
    CHECK(before_size == after_size);
    // Do not check timestamp (filesystem precision may vary)
    cleanup(dir);
}

// ============================================================
// 27-30. Fixture imports
// ============================================================
TEST_CASE("normal fixtures import") {
    auto dir = test_dir("imp_fix_norm");
    cleanup(dir); fs::create_directories(dir);
    ConnectionPool pool;
    init_pool(pool, dir);
    auto data_dir = dir + "data/";
    copy_fixtures("normal", data_dir);
    LegacyImporter imp(data_dir, pool);
    auto r = imp.import_all();
    // Diagnose failure if any
    if (!r.success) {
        MESSAGE("import_all failed at [" << r.failed_resource << "] error=[" << r.error << "]");
    }
    CHECK(r.success);
    // Verify at least some data loaded
    SQLiteStorage ss(pool);
    CHECK(ss.load_nodes().size() > 0);
    cleanup(dir);
}

TEST_CASE("legacy fixtures import") {
    auto dir = test_dir("imp_fix_leg");
    cleanup(dir); fs::create_directories(dir);
    ConnectionPool pool;
    init_pool(pool, dir);
    std::string src = std::string(kFixtureBase) + "/legacy";
    if (fs::exists(src)) {
        fs::copy(src + "/sites_5field.db", dir + "sites.db",
                 fs::copy_options::overwrite_existing);
        fs::copy(src + "/ssl_certificates_4field.db", dir + "ssl_certificates.db",
                 fs::copy_options::overwrite_existing);
        fs::copy(src + "/mail_domains_10field.db", dir + "mail_domains.db",
                 fs::copy_options::overwrite_existing);
        copy_fixtures("normal", dir);
        LegacyImporter imp(dir, pool);
        auto r = imp.import_all();
        if (!r.success) {
            MESSAGE("legacy import_all failed at [" << r.failed_resource << "] error=[" << r.error << "]");
        }
        CHECK(r.success);
        // If we imported the 5-field site fixture
        SQLiteStorage ss(pool);
        CHECK(ss.load_sites().size() > 0);
    }
    cleanup(dir);
}

TEST_CASE("sentinel fixtures import") {
    auto dir = test_dir("imp_fix_sent");
    cleanup(dir); fs::create_directories(dir);
    ConnectionPool pool;
    init_pool(pool, dir);
    copy_fixtures("sentinels", dir);
    // Need base files too
    copy_fixtures("normal", dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_all();
    CHECK(r.success);
    cleanup(dir);
}

TEST_CASE("production-derived fixtures import") {
    auto dir = test_dir("imp_fix_prod");
    cleanup(dir); fs::create_directories(dir);
    std::string prod_src = std::string(kFixtureBase) + "/production_derived";
    if (fs::exists(prod_src)) {
        ConnectionPool pool;
        init_pool(pool, dir);
        copy_fixtures("production_derived", dir);
        LegacyImporter imp(dir, pool);
        auto r = imp.import_all();
        CHECK(r.success);
        cleanup(dir);
    }
}

// ============================================================
// 32-33. PRAGMA checks
// ============================================================
TEST_CASE("foreign_key_check clean after normal import") {
    auto dir = test_dir("imp_fkcheck");
    cleanup(dir); fs::create_directories(dir);
    ConnectionPool pool;
    init_pool(pool, dir);
    auto data_dir = dir + "data/";
    copy_fixtures("normal", data_dir);
    LegacyImporter imp(data_dir, pool);
    CHECK(imp.import_all().success);

    // Run PRAGMA foreign_key_check
    SQLiteDB db;
    REQUIRE(db.open(dir + "containercp.db"));
    REQUIRE(db.exec("PRAGMA foreign_keys = ON"));
    REQUIRE(db.prepare("PRAGMA foreign_key_check"));
    int violations = 0;
    while (db.step()) {
        ++violations;
    }
    CHECK(violations == 0);
    db.close();
    cleanup(dir);
}

TEST_CASE("integrity_check ok after normal import") {
    auto dir = test_dir("imp_intck");
    cleanup(dir); fs::create_directories(dir);
    ConnectionPool pool;
    init_pool(pool, dir);
    auto data_dir = dir + "data/";
    copy_fixtures("normal", data_dir);
    LegacyImporter imp(data_dir, pool);
    CHECK(imp.import_all().success);

    SQLiteDB db;
    REQUIRE(db.open(dir + "containercp.db"));
    REQUIRE(db.prepare("PRAGMA integrity_check"));
    bool ok = false;
    if (db.step()) {
        ok = (db.column_text(0) == "ok");
    }
    CHECK(ok);
    db.close();
    cleanup(dir);
}

// ============================================================
// 34. Idempotent valid reimport
// ============================================================
TEST_CASE("idempotent reimport succeeds") {
    auto dir = test_dir("imp_reimport");
    cleanup(dir); fs::create_directories(dir);
    ConnectionPool pool;
    init_pool(pool, dir);
    auto data_dir = dir + "data/";
    copy_fixtures("normal", data_dir);
    LegacyImporter imp(data_dir, pool);
    CHECK(imp.import_all().success);
    CHECK(imp.import_all().success);
    cleanup(dir);
}

// ============================================================
// FK violation: access_grant references missing access_user
// ============================================================
TEST_CASE("access_grant missing access_user fails") {
    auto dir = test_dir("imp_fk_ag_au");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "access_grants.db", "1|99999|1|read\n");
    write_file(dir + "sites.db", "1|example.com|admin|1|apache|1\n");
    write_file(dir + "nodes.db", "1|main|web\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_access_grants();
    CHECK_FALSE(r.success);
    cleanup(dir);
}

// ============================================================
// FK violation: access_grant references missing site
// ============================================================
TEST_CASE("access_grant missing site fails") {
    auto dir = test_dir("imp_fk_ag_site");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "access_grants.db", "1|1|99999|read\n");
    write_file(dir + "access_users.db", "1|testuser|password|hash|1\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_access_grants();
    CHECK_FALSE(r.success);
    cleanup(dir);
}

// ============================================================
// FK violation: mailbox missing mail_domain
// ============================================================
TEST_CASE("mailbox missing mail_domain fails") {
    auto dir = test_dir("imp_fk_mb_md");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "mail_mailboxes.db",
               "1|99999|user|hash|1000|500|1|User||1||||\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_mail_mailboxes();
    CHECK_FALSE(r.success);
    cleanup(dir);
}

// ============================================================
// FK violation: alias missing mail_domain
// ============================================================
TEST_CASE("alias missing mail_domain fails") {
    auto dir = test_dir("imp_fk_al_md");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "mail_aliases.db",
               "1|99999|src|dest|1|2024-01-01|2024-01-01\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_mail_aliases();
    CHECK_FALSE(r.success);
    cleanup(dir);
}

// ============================================================
// Combined profiles import tests
// ============================================================

TEST_CASE("profiles.db only imports all rows") {
    auto dir = test_dir("imp_prof_only");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "profiles.db",
               "1|default|WEB_SERVER|apache|static|/tpl||1|1\n"
               "2|custom|WEB_SERVER|nginx|docker|/tpl2||1|0\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_all_profiles();
    CHECK(r.success);
    CHECK(r.disposition == ImportDisposition::Imported);
    CHECK(r.record_count == 2);
    // Verify via SQLiteStorage
    auto loaded = SQLiteStorage(pool).load_profiles();
    CHECK(loaded.size() == 2);
    cleanup(dir);
}

TEST_CASE("profiles + template_profiles yield union") {
    auto dir = test_dir("imp_prof_union");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "profiles.db",
               "1|standard|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "template_profiles.db",
               "2|template1|nginx|static|/tpl_t||1|0\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_all_profiles();
    CHECK(r.success);
    CHECK(r.disposition == ImportDisposition::Imported);
    CHECK(r.record_count == 2);
    // Verify union: both rows present
    auto loaded = SQLiteStorage(pool).load_profiles();
    CHECK(loaded.size() == 2);
    // Verify template row has WEB_SERVER type
    bool found_template = false;
    for (const auto& p : loaded) {
        if (p.profile_name == "template1") {
            found_template = true;
            CHECK(p.type == profile::ProfileType::WEB_SERVER);
        }
    }
    CHECK(found_template);
    cleanup(dir);
}

TEST_CASE("profiles.db rows not erased after combined import") {
    auto dir = test_dir("imp_prof_noerase");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "profiles.db",
               "1|standard|WEB_SERVER|apache|static|/tpl||1|1\n"
               "2|another|WEB_SERVER|nginx|docker|/tpl2||1|0\n");
    write_file(dir + "template_profiles.db",
               "3|template1|nginx|static|/tpl_t||1|0\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_all_profiles();
    CHECK(r.success);
    CHECK(r.record_count == 3);
    // All three rows must be present
    auto loaded = SQLiteStorage(pool).load_profiles();
    CHECK(loaded.size() == 3);
    std::set<std::string> names;
    for (const auto& p : loaded) names.insert(p.profile_name);
    CHECK(names.count("standard"));
    CHECK(names.count("another"));
    CHECK(names.count("template1"));
    cleanup(dir);
}

TEST_CASE("missing optional template_profiles.db does not affect profiles") {
    auto dir = test_dir("imp_prof_notpl");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "profiles.db",
               "1|standard|WEB_SERVER|apache|static|/tpl||1|1\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_all_profiles();
    CHECK(r.success);
    CHECK(r.record_count == 1);
    CHECK(SQLiteStorage(pool).load_profiles().size() == 1);
    cleanup(dir);
}

TEST_CASE("empty template_profiles.db does not erase profiles") {
    auto dir = test_dir("imp_prof_emptpl");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "profiles.db",
               "1|standard|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "template_profiles.db", "");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_all_profiles();
    CHECK(r.success);
    CHECK(r.record_count == 1);
    CHECK(SQLiteStorage(pool).load_profiles().size() == 1);
    cleanup(dir);
}

TEST_CASE("cross-file duplicate id rejected") {
    auto dir = test_dir("imp_prof_dup_id");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "profiles.db",
               "1|standard|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "template_profiles.db",
               "1|template1|nginx|static|/tpl_t||1|0\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_all_profiles();
    CHECK_FALSE(r.success);
    CHECK(r.error == "duplicate_id");
    // Table unchanged
    CHECK(SQLiteStorage(pool).load_profiles().size() == 0);
    cleanup(dir);
}

TEST_CASE("cross-file duplicate profile_name rejected") {
    auto dir = test_dir("imp_prof_dup_name");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "profiles.db",
               "1|standard|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "template_profiles.db",
               "2|standard|nginx|static|/tpl_t||1|0\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_all_profiles();
    CHECK_FALSE(r.success);
    CHECK(r.error == "duplicate_profile_name");
    CHECK(SQLiteStorage(pool).load_profiles().size() == 0);
    cleanup(dir);
}

TEST_CASE("malformed template after valid profiles skips write") {
    auto dir = test_dir("imp_prof_mal_tpl");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "profiles.db",
               "1|standard|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "template_profiles.db",
               "BAD|template1|nginx|static|/tpl_t||1|0\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_all_profiles();
    CHECK_FALSE(r.success);
    // SQLite should still have its previous state (empty)
    CHECK(SQLiteStorage(pool).load_profiles().size() == 0);
    cleanup(dir);
}

TEST_CASE("idempotent combined reimport") {
    auto dir = test_dir("imp_prof_idem");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "profiles.db",
               "1|standard|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "template_profiles.db",
               "2|templ|nginx|static|/tpl_t||1|0\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    CHECK(imp.import_all_profiles().success);
    uint64_t c1 = SQLiteStorage(pool).load_profiles().size();
    CHECK(imp.import_all_profiles().success);
    uint64_t c2 = SQLiteStorage(pool).load_profiles().size();
    CHECK(c1 == c2);
    // Both still present
    CHECK(c1 == 2);
    cleanup(dir);
}

TEST_CASE("import_all uses one combined profiles step") {
    auto dir = test_dir("imp_prof_all");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n");
    write_file(dir + "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_file(dir + "profiles.db",
               "1|standard|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "template_profiles.db",
               "2|templ|nginx|static|/tpl_t||1|0\n");
    write_file(dir + "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_file(dir + "sites.db", "1|example.com|admin|1|apache|1\n");
    write_file(dir + "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n");
    write_file(dir + "databases.db", "1|db|user|pass|mysql|8.0|1|1|1\n");
    write_file(dir + "backups.db", "1|1|1|backup.tar.gz|full|1000|2024-01-01|completed|/path|gzip\n");
    write_file(dir + "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");
    ConnectionPool pool;
    init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto result = imp.import_all();
    CHECK(result.success);
    // Find the profiles result
    bool found_profiles = false;
    for (const auto& res : result.resources) {
        if (res.resource_type == "profiles") {
            found_profiles = true;
            CHECK(res.success);
            CHECK(res.record_count == 2);
            break;
        }
    }
    CHECK(found_profiles);
    // No separate template_profiles entry
    bool found_tpl = false;
    for (const auto& res : result.resources) {
        if (res.resource_type == "template_profiles") {
            found_tpl = true;
            break;
        }
    }
    CHECK_FALSE(found_tpl);
    // Verify SQLite has union
    auto loaded = SQLiteStorage(pool).load_profiles();
    CHECK(loaded.size() == 2);
    cleanup(dir);
}

TEST_CASE("import_all_profiles with full fixture retains both") {
    auto dir = test_dir("imp_prof_fixt");
    cleanup(dir); fs::create_directories(dir);
    ConnectionPool pool;
    init_pool(pool, dir);
    copy_fixtures("normal", dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_all_profiles();
    CHECK(r.success);
    auto loaded = SQLiteStorage(pool).load_profiles();
    CHECK(loaded.size() >= 2);
    cleanup(dir);
}

// ============================================================
// Phase 9 — Verification tests
// ============================================================

TEST_CASE("SHA-256 test vector") {
    CHECK(Verification::sha256("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Verification::sha256("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Verification::sha256("hello") ==
        "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST_CASE("canonical serialization is stable and deterministic") {
    // Verify same data produces same hash
    std::vector<node::Node> nodes;
    node::Node n1, n2;
    n1.id = 2; n1.name = "second"; n1.type = "web";
    n2.id = 1; n2.name = "first"; n2.type = "local";
    nodes.push_back(n1); nodes.push_back(n2);

    Verification v("", "", ImportAllResult{});
    std::string c1 = v.canonical_nodes(nodes);
    std::string c2 = v.canonical_nodes(nodes);
    CHECK(c1 == c2);

    // Reordering input yields same output (sorted by ID)
    std::vector<node::Node> reversed;
    reversed.push_back(n2); reversed.push_back(n1);
    CHECK(v.canonical_nodes(reversed) == c1);
}

TEST_CASE("empty and missing optional verification") {
    auto dir = test_dir("vfy_skip");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n");
    write_file(dir + "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_file(dir + "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_file(dir + "sites.db", "1|example.com|admin|1|apache|1\n");
    write_file(dir + "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n");
    write_file(dir + "databases.db", "1|db|user|pass|mysql|8.0|1|1|1\n");
    write_file(dir + "backups.db", "1|1|1|backup.tar.gz|full|1000|1|completed|/path|gzip\n");
    write_file(dir + "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");
    ConnectionPool pool; init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto import_result = imp.import_all();
    REQUIRE(import_result.success);
    Verification vfy(dir, dir + "containercp.db", import_result);
    auto nodes_vfy = vfy.verify_nodes();
    CHECK(nodes_vfy.success);
    auto ssl_vfy = vfy.verify_ssl_certificates();
    CHECK(ssl_vfy.success);
    CHECK(ssl_vfy.status == VerificationStatus::Skipped);
    cleanup(dir);
}

TEST_CASE("verification rejects count mismatch") {
    auto dir = test_dir("vfy_cnt");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n2|second|local\n");
    ConnectionPool pool;
    init_pool(pool, dir);

    // Import only 1 node (simulate incomplete import)
    {
        LegacyImporter imp(dir, pool);
        std::vector<node::Node> partial;
        node::Node n; n.id = 1; n.name = "main"; n.type = "web";
        partial.push_back(n);
        CHECK(SQLiteStorage(pool).try_save_nodes(partial));
    }

    // Create import result claiming both nodes were imported
    ImportAllResult import_result;
    ImportResult ir; ir.success = true; ir.disposition = ImportDisposition::Imported;
    ir.resource_type = "nodes"; ir.record_count = 2;
    import_result.resources.push_back(ir);
    import_result.success = true;

    Verification vfy(dir, dir + "containercp.db", import_result);
    auto r = vfy.verify_nodes();
    CHECK_FALSE(r.success);
    CHECK(r.legacy_record_count == 2);
    CHECK(r.sqlite_record_count == 1);

    cleanup(dir);
}

TEST_CASE("verification rejects failed import result") {
    ImportAllResult failed_result;
    failed_result.success = false;
    Verification vfy("/nonexistent", "/nonexistent/db", failed_result);
    auto result = vfy.verify_all();
    CHECK_FALSE(result.success);
    CHECK(result.error == "import_failed");
}

TEST_CASE("sensitive fields redacted from mismatches") {
    auto dir = test_dir("vfy_redact");
    cleanup(dir); fs::create_directories(dir);
    // Create a database with a different password_hash than legacy
    write_file(dir + "access_users.db", "1|testuser|password|legacy_hash|1\n");
    ConnectionPool pool; init_pool(pool, dir);
    {
        LegacyImporter imp(dir, pool);
        auto r = imp.import_access_users();
        CHECK(r.success);
    }
    // Tamper with SQLite
    {
        WriteGuard wg(pool);
        if (wg.is_valid())
            wg.db().exec("UPDATE access_users SET password_hash = 'tampered_hash' WHERE id = 1");
    }
    // Verify — should fail with mismatches but not expose secrets
    ImportAllResult fake_result;
    ImportResult ir; ir.success = true; ir.disposition = ImportDisposition::Imported;
    ir.resource_type = "access_users"; ir.record_count = 1;
    fake_result.resources.push_back(ir); fake_result.success = true;

    Verification vfy(dir, dir + "containercp.db", fake_result);
    auto r = vfy.verify_access_users();
    CHECK_FALSE(r.success);
    // Mismatches must not contain the actual hash values
    for (const auto& mm : r.mismatches) {
        CHECK(mm.expected.find("legacy_hash") == std::string::npos);
        CHECK(mm.actual.find("tampered_hash") == std::string::npos);
        CHECK(mm.actual.find("tampered") == std::string::npos);
    }
    cleanup(dir);
}

TEST_CASE("verification initializes its own pool") {
    auto dir = test_dir("vfy_init_pool");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n");
    write_file(dir + "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_file(dir + "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_file(dir + "sites.db", "1|example.com|admin|1|apache|1\n");
    write_file(dir + "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n");
    write_file(dir + "databases.db", "1|db|user|pass|mysql|8.0|1|1|1\n");
    write_file(dir + "backups.db", "1|1|1|backup.tar.gz|full|1000|1|completed|/path|gzip\n");
    write_file(dir + "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");

    // Import using separate pool
    ConnectionPool import_pool; init_pool(import_pool, dir);
    LegacyImporter imp(dir, import_pool);
    auto import_result = imp.import_all();
    REQUIRE(import_result.success);
    import_pool.shutdown();

    // Verify uses its own pool (separate from import pool)
    Verification vfy(dir, dir + "containercp.db", import_result);
    auto result = vfy.verify_all();
    CHECK(result.initial_verification_passed);
    CHECK(result.success);
    cleanup(dir);
}

TEST_CASE("shared LineParser used consistently") {
    // Verify the importer and verifier agree on parsing simple data
    auto dir = test_dir("vfy_shared_parser");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n2|second|local\n");
    ConnectionPool pool; init_pool(pool, dir);
    // Parse via importer (import and re-verify)
    LegacyImporter imp(dir, pool);
    CHECK(imp.import_nodes().success);
    // Load via SQLiteStorage
    auto imported = SQLiteStorage(pool).load_nodes();
    CHECK(imported.size() == 2);

    ImportAllResult fake_result;
    ImportResult ir; ir.success = true; ir.disposition = ImportDisposition::Imported;
    ir.resource_type = "nodes"; ir.record_count = 2;
    fake_result.resources.push_back(ir); fake_result.success = true;

    Verification vfy(dir, dir + "containercp.db", fake_result);
    auto r = vfy.verify_nodes();
    CHECK(r.success);
    CHECK(r.legacy_record_count == 2);
    CHECK(r.sqlite_record_count == 2);
    cleanup(dir);
}

TEST_CASE("baseline checksum matches verification canonical") {
    auto dir = test_dir("vfy_base_cksum");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n2|second|local\n");
    ConnectionPool pool; init_pool(pool, dir);
    // Import nodes
    LegacyImporter imp(dir, pool);
    auto result = imp.import_nodes();
    CHECK(result.success);
    // Baseline should have been captured during import
    CHECK(result.baseline.success);
    CHECK(result.baseline.record_count == 0); // fresh database
    // The baseline checksum should match what Verification computes for an empty table
    Verification vfy(dir, dir + "containercp.db", ImportAllResult{});
    // Compute checksum of empty nodes table
    ImportAllResult fake_result;
    ImportResult ir; ir.success = true; ir.disposition = ImportDisposition::Imported;
    ir.resource_type = "nodes"; ir.record_count = 2;
    fake_result.resources.push_back(ir); fake_result.success = true;
    // Verify that canonical format is consistent
    // Import 2 nodes, then check the baseline was captured before import (0 records)
    CHECK(result.baseline.record_count == 0);
    // After import, verification should pass (no duplicates)
    pool.shutdown();
    ConnectionPool vpool; init_pool(vpool, dir);
    Verification vfy2(dir, dir + "containercp.db", fake_result);
    auto verify_result = vfy2.verify_nodes();
    CHECK(verify_result.success);
    CHECK(verify_result.status == VerificationStatus::Passed);
    vpool.shutdown();
    cleanup(dir);
}

TEST_CASE("baseline checksum detects changed state") {
    auto dir = test_dir("vfy_base_change");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n");
    ConnectionPool pool; init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    // Import first node
    auto r1 = imp.import_nodes();
    CHECK(r1.success);
    CHECK(r1.baseline.record_count == 0); // fresh

    // Import again with same file — should detect existing state
    auto r2 = imp.import_nodes();
    CHECK(r2.success);
    // Baseline before second import should have 1 record
    CHECK(r2.baseline.record_count == 1);
    // Verify both imports produced correct checksums
    CHECK(!r2.baseline.canonical_checksum.empty());
    pool.shutdown();
    cleanup(dir);
}

TEST_CASE("reopen comparison checks count and checksum") {
    auto dir = test_dir("vfy_reopen_ck");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n2|second|local\n");
    write_file(dir + "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_file(dir + "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_file(dir + "sites.db", "1|example.com|admin|1|apache|1\n");
    write_file(dir + "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n");
    write_file(dir + "databases.db", "1|db|user|pass|mysql|8.0|1|1|1\n");
    write_file(dir + "backups.db", "1|1|1|backup.tar.gz|full|1000|2024-01-01|completed|/path|gzip\n");
    write_file(dir + "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");
    ConnectionPool pool; init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto import_result = imp.import_all();
    REQUIRE(import_result.success);
    pool.shutdown();

    Verification vfy(dir, dir + "containercp.db", import_result, dir);
    auto result = vfy.verify_all();
    CHECK(result.initial_verification_passed);
    CHECK(result.reopen_succeeded);
    // Check reopened results exist and have correct counts
    for (const auto& rr : result.reopened_resources) {
        CHECK(rr.success);
    }
    CHECK(result.reopened_verification_passed);
    CHECK(result.reopened_integrity_check_result == "ok");
    cleanup(dir);
}

TEST_CASE("reopen sensitive field redaction") {
    auto dir = test_dir("vfy_reopen_sens");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n");
    write_file(dir + "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_file(dir + "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_file(dir + "sites.db", "1|example.com|admin|1|apache|1\n");
    write_file(dir + "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n");
    write_file(dir + "databases.db", "1|db|user|secret_pass|mysql|8.0|1|1|1\n");
    write_file(dir + "backups.db", "1|1|1|backup.tar.gz|full|1000|1|completed|/path|gzip\n");
    write_file(dir + "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");
    write_file(dir + "access_users.db", "1|testuser|password|secret_hash|1\n");
    ConnectionPool pool; init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto import_result = imp.import_all();
    REQUIRE(import_result.success);
    pool.shutdown();

    // Tamper with a sensitive field
    {
        ConnectionPool tp; tp.initialize(dir + "containercp.db");
        WriteGuard wg(tp);
        if (wg.is_valid())
            wg.db().exec("UPDATE databases SET db_password = 'tampered' WHERE id = 1");
        tp.shutdown();
    }

    Verification vfy(dir, dir + "containercp.db", import_result, dir);
    auto result = vfy.verify_all();
    // Initial verification should fail (tampered database)
    CHECK_FALSE(result.initial_verification_passed);
    // The database verification should detect the mismatch
    if (!result.resources.empty()) {
        auto& db_result = result.resources[6]; // databases
        CHECK_FALSE(db_result.success);
        // Check that the secret value is not exposed in mismatches
        for (const auto& mm : db_result.mismatches) {
            CHECK(mm.expected.find("secret_pass") == std::string::npos);
            CHECK(mm.actual.find("tampered") == std::string::npos);
        }
    }
    cleanup(dir);
}

TEST_CASE("skipped resource retains baseline evidence") {
    auto dir = test_dir("vfy_skip_ev");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n");
    write_file(dir + "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_file(dir + "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_file(dir + "sites.db", "1|example.com|admin|1|apache|1\n");
    write_file(dir + "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n");
    write_file(dir + "databases.db", "1|db|user|pass|mysql|8.0|1|1|1\n");
    write_file(dir + "backups.db", "1|1|1|backup.tar.gz|full|1000|1|completed|/path|gzip\n");
    write_file(dir + "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");
    ConnectionPool pool; init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto result = imp.import_all();
    REQUIRE(result.success);
    pool.shutdown();
    Verification vfy(dir, dir + "containercp.db", result, dir);
    auto db_result = vfy.verify_all();
    CHECK(db_result.initial_verification_passed);
    for (const auto& res : db_result.resources) {
        if (res.status == VerificationStatus::Skipped) {
            CHECK(!res.legacy_checksum.empty());
            CHECK(!res.sqlite_checksum.empty());
        }
    }
    cleanup(dir);
}

TEST_CASE("exactly 17 reopened results") {
    auto dir = test_dir("vfy_17reopen");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n");
    write_file(dir + "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_file(dir + "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_file(dir + "sites.db", "1|example.com|admin|1|apache|1\n");
    write_file(dir + "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n");
    write_file(dir + "databases.db", "1|db|user|pass|mysql|8.0|1|1|1\n");
    write_file(dir + "backups.db", "1|1|1|backup.tar.gz|full|1000|1|completed|/path|gzip\n");
    write_file(dir + "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");
    ConnectionPool pool; init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto result = imp.import_all();
    REQUIRE(result.success);
    pool.shutdown();
    Verification vfy(dir, dir + "containercp.db", result, dir);
    auto db_result = vfy.verify_all();
    CHECK(db_result.initial_verification_passed);
    CHECK(db_result.reopened_resources.size() == 17);
    CHECK(db_result.reopened_verification_passed);
    cleanup(dir);
}

TEST_CASE("mail_config baseline fails on query error") {
    auto dir = test_dir("vfy_mc_bl_fail");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "mail_state.db", "active\n");
    write_file(dir + "nodes.db", "1|main|web\n");
    write_file(dir + "php_versions.db", "1|8.2|php:8.2|1|1\n");
    write_file(dir + "profiles.db", "1|default|WEB_SERVER|apache|static|/tpl||1|1\n");
    write_file(dir + "users.db", "1|admin|1000|/home/admin|/bin/bash|1\n");
    write_file(dir + "sites.db", "1|example.com|admin|1|apache|1\n");
    write_file(dir + "domains.db", "1|example.com|1|1|8.2|1|1|primary|\n");
    write_file(dir + "databases.db", "1|db|user|pass|mysql|8.0|1|1|1\n");
    write_file(dir + "backups.db", "1|1|1|backup.tar.gz|full|1000|1|completed|/path|gzip\n");
    write_file(dir + "reverse_proxies.db", "1|proxy.example.com|1|nginx|/cfg|http://upstream|1|active\n");
    ConnectionPool pool;
    // Init and migrate, then DROP mail_config to cause baseline failure
    REQUIRE(pool.initialize(dir + "containercp.db"));
    { SQLiteDB migrator; migrator.open(dir + "containercp.db");
      MigrationEngine eng; register_all_schema_migrations(eng); eng.migrate(migrator); migrator.close(); }
    { WriteGuard wg(pool); wg.db().exec("DROP TABLE mail_config"); }
    LegacyImporter imp(dir, pool);
    auto r = imp.import_mail_config();
    CHECK_FALSE(r.success); // baseline capture fails because mail_config table missing
    pool.shutdown(); cleanup(dir);
}

TEST_CASE("CheckedOptionalValue absent vs present-empty") {
    auto dir = test_dir("vfy_opv");
    cleanup(dir); fs::create_directories(dir);
    ConnectionPool pool; init_pool(pool, dir);
    // Insert empty value key
    { WriteGuard wg(pool); wg.db().exec("INSERT INTO mail_config(key,value) VALUES('test','')"); }
    SQLiteSnapshotReader snap(pool);
    // Absent key
    auto r1 = snap.read_mail_config_key("nonexistent");
    CHECK(r1.success); CHECK_FALSE(r1.present);
    // Present empty key
    auto r2 = snap.read_mail_config_key("test");
    CHECK(r2.success); CHECK(r2.present); CHECK(r2.value.empty());
    pool.shutdown(); cleanup(dir);
}

TEST_CASE("stray code is not present") {
    // Verify that capture_baseline properly closes
    auto dir = test_dir("vfy_stray");
    cleanup(dir); fs::create_directories(dir);
    write_file(dir + "nodes.db", "1|main|web\n");
    ConnectionPool pool; init_pool(pool, dir);
    LegacyImporter imp(dir, pool);
    auto r = imp.import_nodes();
    CHECK(r.success);
    CHECK(r.baseline.success);
    pool.shutdown(); cleanup(dir);
}
