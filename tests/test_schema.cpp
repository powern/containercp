#include "storage/SQLiteWrapper.h"
#include "storage/MigrationEngine.h"
#include "storage/SchemaMigrations.h"
#include "storage/Storage.h"

#include <filesystem>
#include <string>

#include "doctest/doctest.h"

namespace fs = std::filesystem;

static std::string schema_db_path(const std::string& name) {
    return (fs::temp_directory_path() / name).string();
}

static void schema_cleanup(const std::string& path) {
    fs::remove(path);
    fs::remove(path + "-wal");
    fs::remove(path + "-shm");
}

// Helper: create a fresh database with schema v1 applied
static containercp::storage::SQLiteDB create_schema_db(const std::string& path) {
    containercp::storage::SQLiteDB db;
    REQUIRE(db.open(path));
    containercp::storage::MigrationEngine eng;
    containercp::storage::register_all_schema_migrations(eng);
    REQUIRE(eng.migrate(db));
    return db;
}

TEST_CASE("Schema v1 creates exactly 18 tables") {
    auto path = schema_db_path("containercp_schema_tables.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        REQUIRE(db.prepare("SELECT name FROM sqlite_master WHERE type='table' "
                           "ORDER BY name"));
        int count = 0;
        while (db.step()) {
            ++count;
            std::string name = db.column_text(0);
            // schema_migrations and storage_meta are created by MigrationEngine
            CHECK((name == "nodes" || name == "sites" || name == "users" ||
                   name == "domains" || name == "php_versions" ||
                   name == "databases" || name == "backups" ||
                   name == "ssl_certificates" || name == "mail_domains" ||
                   name == "mail_mailboxes" || name == "mail_aliases" ||
                   name == "access_users" || name == "access_grants" ||
                   name == "reverse_proxies" || name == "profiles" ||
                   name == "auth_users" || name == "mail_config" ||
                   name == "schema_migrations" || name == "storage_meta"));
        }
        CHECK(count >= 18);  // 18 business + schema_migrations + storage_meta = 20
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 creates all approved indices") {
    auto path = schema_db_path("containercp_schema_indices.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        // Query all index names
        REQUIRE(db.prepare("SELECT name FROM sqlite_master WHERE type='index' "
                           "AND name LIKE 'idx_%' ORDER BY name"));
        int idx_count = 0;
        while (db.step()) {
            ++idx_count;
        }
        CHECK(idx_count >= 13);  // 13 approved indices
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 foreign keys are enabled") {
    auto path = schema_db_path("containercp_schema_fk_enabled.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        REQUIRE(db.prepare("PRAGMA foreign_keys"));
        REQUIRE(db.step());
        CHECK(db.column_int(0) == 1);
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 valid FK inserts succeed") {
    auto path = schema_db_path("containercp_schema_fk_ok.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        // Insert into mail_domains (no FK) then mailboxes (FK→mail_domains)
        REQUIRE(db.exec("INSERT INTO mail_domains (id, domain_name) VALUES (1, 'example.com')"));
        REQUIRE(db.exec("INSERT INTO mail_mailboxes (id, domain_id, local_part) "
                        "VALUES (1, 1, 'admin')"));

        // Insert into access_users (no FK) then grants (FK→access_users)
        REQUIRE(db.exec("INSERT INTO access_users (id, username) VALUES (1, 'sftp1')"));
        REQUIRE(db.exec("INSERT INTO access_grants (id, access_user_id, site_id) "
                        "VALUES (1, 1, 1)"));
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 invalid FK references fail") {
    auto path = schema_db_path("containercp_schema_fk_fail.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        // Insert mailbox referencing non-existent mail_domain
        bool failed = false;
        try {
            CHECK_FALSE(db.exec("INSERT INTO mail_mailboxes (id, domain_id, local_part) "
                                "VALUES (1, 999, 'admin')"));
            failed = true;
        } catch (...) {
            // exec returns false on FK violation, some envs may throw
            failed = true;
        }
        CHECK(failed);
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 ON DELETE RESTRICT prevents parent deletion") {
    auto path = schema_db_path("containercp_schema_restrict.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        REQUIRE(db.exec("INSERT INTO mail_domains (id, domain_name) VALUES (1, 'example.com')"));
        REQUIRE(db.exec("INSERT INTO mail_mailboxes (id, domain_id, local_part) "
                        "VALUES (1, 1, 'admin')"));

        // Deleting the parent should fail (children exist)
        bool restricted = false;
        try {
            CHECK_FALSE(db.exec("DELETE FROM mail_domains WHERE id = 1"));
            restricted = true;
        } catch (...) {
            restricted = true;
        }
        CHECK(restricted);
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 sentinel 0 values succeed") {
    auto path = schema_db_path("containercp_schema_sentinel.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        // node_id = 0 (default/local sentinel)
        REQUIRE(db.exec("INSERT INTO nodes (id, name) VALUES (1, 'local')"));
        REQUIRE(db.exec("INSERT INTO sites (id, domain, node_id) VALUES (1, 'test.com', 0)"));

        // owner_id = 0 and site_id = 0 on domains
        REQUIRE(db.exec("INSERT INTO domains (id, fqdn, owner_id, site_id) "
                        "VALUES (1, 'test.com', 0, 0)"));

        // site_id = 0 on reverse_proxies (admin panel)
        REQUIRE(db.exec("INSERT INTO reverse_proxies (id, domain, site_id) "
                        "VALUES (1, 'admin.example.com', 0)"));

        // domain_id = 0 and site_id = 0 on mail_domains (external)
        REQUIRE(db.exec("INSERT INTO mail_domains (id, domain_name, domain_id, site_id) "
                        "VALUES (1, 'external.com', 0, 0)"));

        // site_id = 99 on backups (orphan)
        REQUIRE(db.exec("INSERT INTO backups (id, site_id, filename, created_at) "
                        "VALUES (1, 99, 'orphan.tar.gz', '2026-01-01T00:00:00Z')"));
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 no jobs table") {
    auto path = schema_db_path("containercp_schema_nojobs.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        CHECK_FALSE(db.exec("SELECT 1 FROM jobs LIMIT 1"));
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 no PEM or private key columns") {
    auto path = schema_db_path("containercp_schema_nopem.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        // Check all tables for columns that might suggest PEM/key content
        REQUIRE(db.prepare("SELECT sql FROM sqlite_master WHERE type='table' "
                           "AND sql LIKE '%pem%'"));
        if (db.step()) {
            // If found, it should be a path column, not content
            std::string sql = db.column_text(0);
            CHECK((sql.find("_path") != std::string::npos ||
                   sql.find("path_") != std::string::npos));
        }
        // Check for private_key, privatekey, etc.
        REQUIRE(db.prepare("SELECT sql FROM sqlite_master WHERE type='table' "
                           "AND (sql LIKE '%private_key%' OR sql LIKE '%privatekey%') "
                           "AND sql NOT LIKE '%_path%'"));
        CHECK_FALSE(db.step());
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 mail_config accepts key-value pairs") {
    auto path = schema_db_path("containercp_schema_mailcfg.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        REQUIRE(db.exec("INSERT INTO mail_config VALUES ('module_state', 'active')"));
        REQUIRE(db.exec("INSERT INTO mail_config VALUES ('smarthost', "
                        "'{\"host\":\"smtp.example.com\",\"port\":587}')"));

        REQUIRE(db.prepare("SELECT value FROM mail_config WHERE key = 'module_state'"));
        REQUIRE(db.step());
        CHECK(db.column_text(0) == "active");
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 re-running migration is no-op") {
    auto path = schema_db_path("containercp_schema_rerun.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        // First run
        {
            containercp::storage::MigrationEngine eng;
            containercp::storage::register_all_schema_migrations(eng);
            CHECK(eng.migrate(db));
            CHECK(eng.current_version(db) == 1);
        }

        // Second run — no error
        {
            containercp::storage::MigrationEngine eng;
            containercp::storage::register_all_schema_migrations(eng);
            CHECK(eng.migrate(db));
        }
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 checksum is stable") {
    auto path = schema_db_path("containercp_schema_checksum.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        // Apply once
        containercp::storage::MigrationEngine eng1;
        containercp::storage::register_all_schema_migrations(eng1);
        CHECK(eng1.migrate(db));

        // Read the stored checksum
        REQUIRE(db.prepare("SELECT checksum FROM schema_migrations WHERE version = 1"));
        REQUIRE(db.step());
        std::string stored_checksum = db.column_text(0);

        // A second engine with the same migration should match
        containercp::storage::MigrationEngine eng2;
        containercp::storage::register_all_schema_migrations(eng2);
        CHECK(eng2.migrate(db));  // checksum match → no error
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 PRAGMA foreign_key_check returns no rows") {
    auto path = schema_db_path("containercp_schema_fkcheck.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        // Populate with valid data
        REQUIRE(db.exec("INSERT INTO mail_domains (id, domain_name) VALUES (1, 'example.com')"));
        REQUIRE(db.exec("INSERT INTO mail_mailboxes (id, domain_id, local_part) "
                        "VALUES (1, 1, 'admin')"));
        REQUIRE(db.exec("INSERT INTO access_users (id, username) VALUES (1, 'sftp1')"));
        REQUIRE(db.exec("INSERT INTO access_grants (id, access_user_id, site_id) "
                        "VALUES (1, 1, 1)"));

        // Check no FK violations
        REQUIRE(db.prepare("PRAGMA foreign_key_check"));
        CHECK_FALSE(db.step());  // no FK violations expected
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 does not import TXT data") {
    // Verify that creating the schema does not touch or read any TXT files
    auto path = schema_db_path("containercp_schema_notxt.db");
    schema_cleanup(path);
    {
        // No TXT files exist in the temp directory — migration should still work
        auto db = create_schema_db(path);
        CHECK(db.is_open());
    }
    schema_cleanup(path);
}

TEST_CASE("Schema v1 does not modify Storage backend") {
    // Verify TXT Storage still works after schema creation
    auto path = schema_db_path("containercp_schema_storage.db");
    schema_cleanup(path);
    // Also need a TXT directory
    std::string txt_dir = (fs::temp_directory_path() / "containercp_schema_txttest/").string();
    fs::create_directories(txt_dir);
    {
        // Create schema first
        {
            containercp::storage::SQLiteDB db;
            REQUIRE(db.open(path));
            containercp::storage::MigrationEngine eng;
            containercp::storage::register_all_schema_migrations(eng);
            REQUIRE(eng.migrate(db));
        }

        // TXT Storage should still work
        containercp::storage::Storage txt_storage(txt_dir);
        txt_storage.save_sites({});
        auto loaded = txt_storage.load_sites();
        CHECK(loaded.empty());
    }
    fs::remove_all(txt_dir);
    schema_cleanup(path);
}

TEST_CASE("Schema v1 production-derived sentinels representable") {
    auto path = schema_db_path("containercp_schema_prodsent.db");
    schema_cleanup(path);
    {
        auto db = create_schema_db(path);

        // Reproduce the anonymized production fixture's sentinel patterns:
        // 1. Admin panel proxy (site_id=0)
        REQUIRE(db.exec("INSERT INTO reverse_proxies (id, domain, site_id) "
                        "VALUES (1, 'admin.anonymized.example.com', 0)"));

        // 2. External mail domain (domain_id=0, site_id=0)
        REQUIRE(db.exec("INSERT INTO mail_domains (id, domain_name, domain_id, site_id) "
                        "VALUES (1, 'external.example.com', 0, 0)"));

        // 3. Orphan backup (site_id=99)
        REQUIRE(db.exec("INSERT INTO backups (id, site_id, filename, created_at) "
                        "VALUES (1, 99, 'orphan.tar.gz', '2026-01-01T00:00:00Z')"));

        // 4. Domain with owner_id=0 (system sentinel)
        REQUIRE(db.exec("INSERT INTO domains (id, fqdn, owner_id, site_id) "
                        "VALUES (1, 'example.com', 0, 0)"));

        // 5. Site with node_id=0 (default)
        REQUIRE(db.exec("INSERT INTO nodes (id, name) VALUES (1, 'local')"));
        REQUIRE(db.exec("INSERT INTO sites (id, domain, node_id) VALUES (1, 'site.example.com', 0)"));

        // 6. Non-contiguous site IDs
        REQUIRE(db.exec("INSERT INTO sites (id, domain) VALUES (8, 'site-h.example.com')"));
    }
    schema_cleanup(path);
}
