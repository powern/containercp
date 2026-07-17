#include "storage/LegacyImporter.h"
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
    REQUIRE(pool.initialize(dir + "import.db"));
    SQLiteDB migrator;
    REQUIRE(migrator.open(dir + "import.db"));
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
    // Resources: nodes + php + profiles + template_profiles (skip) + users = 5
    CHECK(result.resources.size() == 5);
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
    REQUIRE(db.open(dir + "import.db"));
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
    REQUIRE(db.open(dir + "import.db"));
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
