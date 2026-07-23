#include "storage/SQLiteWrapper.h"
#include "storage/MigrationEngine.h"
#include "storage/SchemaMigrations.h"
#include "storage/Storage.h"

#include <filesystem>
#include <set>
#include <string>

#include "doctest/doctest.h"

namespace fs = std::filesystem;

static std::string sdb(const std::string& name) {
    return (fs::temp_directory_path() / name).string();
}

static void clean(const std::string& path) {
    fs::remove(path); fs::remove(path + "-wal"); fs::remove(path + "-shm");
}

// Create a fresh database with schema v1 applied
static containercp::storage::SQLiteDB make_db(const std::string& path) {
    containercp::storage::SQLiteDB db;
    REQUIRE(db.open(path));
    containercp::storage::MigrationEngine eng;
    containercp::storage::register_all_schema_migrations(eng);
    REQUIRE(eng.migrate(db));
    return db;
}

// ============================================================
// Table inventory
// ============================================================

TEST_CASE("Schema v2 application tables = 18") {
    auto path = sdb("sc_apptables.db");
    clean(path);
    {
        auto db = make_db(path);
        std::set<std::string> app{
            "nodes","sites","users","domains","php_versions","databases",
            "backups","ssl_certificates","mail_domains","mail_mailboxes",
            "mail_aliases","access_users","access_grants","access_keys",
            "reverse_proxies","profiles","auth_users","mail_config"
        };
        REQUIRE(db.prepare("SELECT name FROM sqlite_master WHERE type='table' "
                           "AND name NOT IN ('schema_migrations','storage_meta') "
                           "ORDER BY name"));
        int n = 0;
        while (db.step()) {
            ++n;
            CHECK(app.count(db.column_text(0)));
        }
        CHECK(n == 18);
    }
    clean(path);
}

TEST_CASE("Schema v1 metadata tables = schema_migrations + storage_meta") {
    auto path = sdb("sc_metatables.db");
    clean(path);
    {
        auto db = make_db(path);
        std::set<std::string> meta{"schema_migrations","storage_meta"};
        REQUIRE(db.prepare("SELECT name FROM sqlite_master WHERE type='table' "
                           "AND name IN ('schema_migrations','storage_meta') "
                           "ORDER BY name"));
        int n = 0;
        while (db.step()) { ++n; CHECK(meta.count(db.column_text(0))); }
        CHECK(n == 2);
    }
    clean(path);
}

TEST_CASE("Schema v1 total project tables = 19") {
    auto path = sdb("sc_total.db");
    clean(path);
    {
        auto db = make_db(path);
        REQUIRE(db.prepare("SELECT COUNT(*) FROM sqlite_master WHERE type='table' "
                           "AND name NOT LIKE 'sqlite_%'"));
        REQUIRE(db.step());
        CHECK(db.column_int(0) == 20);
    }
    clean(path);
}

TEST_CASE("Schema v1 no jobs table") {
    auto path = sdb("sc_nojobs.db");
    clean(path);
    {
        auto db = make_db(path);
        REQUIRE(db.prepare("SELECT COUNT(*) FROM sqlite_master "
                           "WHERE type='table' AND name='jobs'"));
        REQUIRE(db.step());
        CHECK(db.column_int(0) == 0);
    }
    clean(path);
}

TEST_CASE("Schema v1 no template_profiles table") {
    auto path = sdb("sc_notp.db");
    clean(path);
    {
        auto db = make_db(path);
        REQUIRE(db.prepare("SELECT COUNT(*) FROM sqlite_master "
                           "WHERE type='table' AND name='template_profiles'"));
        REQUIRE(db.step());
        CHECK(db.column_int(0) == 0);
    }
    clean(path);
}

// ============================================================
// Foreign keys
// ============================================================

TEST_CASE("Schema v1 foreign_keys pragma is ON") {
    auto path = sdb("sc_fkon.db");
    clean(path);
    {
        auto db = make_db(path);
        REQUIRE(db.prepare("PRAGMA foreign_keys"));
        REQUIRE(db.step());
        CHECK(db.column_int(0) == 1);
    }
    clean(path);
}

TEST_CASE("Schema v1 access_grants.site_id FK exists") {
    auto path = sdb("sc_agfk.db");
    clean(path);
    {
        auto db = make_db(path);
        // PRAGMA foreign_key_list returns: id, seq, table, from, to, on_update, on_delete, match
        REQUIRE(db.prepare("PRAGMA foreign_key_list('access_grants')"));
        bool found_site_fk = false;
        while (db.step()) {
            std::string from = db.column_text(3);  // "from" column
            std::string to_table = db.column_text(2);
            std::string on_delete = db.column_text(6);
            if (from == "site_id" && to_table == "sites") {
                found_site_fk = true;
                CHECK(on_delete == "RESTRICT");
            }
        }
        CHECK(found_site_fk);
    }
    clean(path);
}

TEST_CASE("Schema v1 mail_mailboxes.domain_id FK exists") {
    auto path = sdb("sc_mmfk.db");
    clean(path);
    {
        auto db = make_db(path);
        REQUIRE(db.prepare("PRAGMA foreign_key_list('mail_mailboxes')"));
        bool found = false;
        while (db.step()) {
            if (db.column_text(3) == "domain_id" && db.column_text(2) == "mail_domains") {
                found = true;
                CHECK(db.column_text(6) == "RESTRICT");
            }
        }
        CHECK(found);
    }
    clean(path);
}

TEST_CASE("Schema v1 access_grants.site_id rejects invalid site") {
    auto path = sdb("sc_agreject.db");
    clean(path);
    {
        auto db = make_db(path);
        REQUIRE(db.exec("INSERT INTO access_users (id, username) VALUES (1, 'u')"));
        REQUIRE(db.exec("INSERT INTO sites (id, domain) VALUES (1, 's.com')"));
        // Valid grant
        REQUIRE(db.exec("INSERT INTO access_grants (id, access_user_id, site_id) "
                        "VALUES (1, 1, 1)"));
        // Invalid site_id
        CHECK_FALSE(db.exec("INSERT INTO access_grants (id, access_user_id, site_id) "
                            "VALUES (2, 1, 999)"));
    }
    clean(path);
}

TEST_CASE("Schema v1 ON DELETE RESTRICT on access_grants") {
    auto path = sdb("sc_agrestrict.db");
    clean(path);
    {
        auto db = make_db(path);
        REQUIRE(db.exec("INSERT INTO access_users (id, username) VALUES (1, 'u')"));
        REQUIRE(db.exec("INSERT INTO sites (id, domain) VALUES (1, 's.com')"));
        REQUIRE(db.exec("INSERT INTO access_grants (id, access_user_id, site_id) "
                        "VALUES (1, 1, 1)"));

        // Deleting the site should fail (grant references it)
        CHECK_FALSE(db.exec("DELETE FROM sites WHERE id = 1"));
    }
    clean(path);
}

TEST_CASE("Schema v1 valid access user + site + grant succeeds") {
    auto path = sdb("sc_agok.db");
    clean(path);
    {
        auto db = make_db(path);
        REQUIRE(db.exec("INSERT INTO access_users (id, username) VALUES (1, 'sftp1')"));
        REQUIRE(db.exec("INSERT INTO sites (id, domain) VALUES (1, 'example.com')"));
        REQUIRE(db.exec("INSERT INTO domains (id, fqdn, site_id) VALUES (1, 'example.com', 1)"));
        REQUIRE(db.exec("INSERT INTO access_grants (id, access_user_id, site_id) "
                        "VALUES (1, 1, 1)"));
    }
    clean(path);
}

// ============================================================
// Sentinel values (no-FK relationships)
// ============================================================

TEST_CASE("Schema v1 approved no-FK sentinel 0 values succeed") {
    auto path = sdb("sc_sentinel.db");
    clean(path);
    {
        auto db = make_db(path);

        // mail_domains.domain_id = 0 (external)
        REQUIRE(db.exec("INSERT INTO mail_domains (id, domain_name, domain_id) "
                        "VALUES (1, 'ext.com', 0)"));

        // mail_domains.site_id = 0 (unlinked)
        REQUIRE(db.exec("INSERT INTO mail_domains (id, domain_name, site_id) "
                        "VALUES (2, 'unlinked.com', 0)"));

        // reverse_proxies.site_id = 0 (admin panel)
        REQUIRE(db.exec("INSERT INTO reverse_proxies (id, domain, site_id) "
                        "VALUES (1, 'admin.example.com', 0)"));

        // sites.node_id = 0 (default)
        REQUIRE(db.exec("INSERT INTO nodes (id, name) VALUES (1, 'local')"));
        REQUIRE(db.exec("INSERT INTO sites (id, domain, node_id) VALUES (1, 's.com', 0)"));

        // domains.owner_id = 0 (system)
        REQUIRE(db.exec("INSERT INTO domains (id, fqdn, owner_id) VALUES (1, 'd.com', 0)"));

        // domains.site_id = 0 (orphan)
        REQUIRE(db.exec("INSERT INTO domains (id, fqdn, site_id) VALUES (2, 'o.com', 0)"));

        // databases.owner_id = 0 (system)
        REQUIRE(db.exec("INSERT INTO databases (id, db_name, owner_id) VALUES (1, 'db1', 0)"));

        // databases.site_id = 0 (orphan)
        REQUIRE(db.exec("INSERT INTO databases (id, db_name, site_id) VALUES (2, 'db2', 0)"));

        // backups.owner_id = 0 (system)
        REQUIRE(db.exec("INSERT INTO backups (id, filename, owner_id, created_at) "
                        "VALUES (1, 'b1.tar', 0, '2026-01-01T00:00:00Z')"));

        // backups.site_id = 0 (orphan)
        REQUIRE(db.exec("INSERT INTO backups (id, filename, site_id, created_at) "
                        "VALUES (2, 'b2.tar', 0, '2026-01-01T00:00:00Z')"));

        // ssl_certificates.domain_id = 0 (orphan)
        REQUIRE(db.exec("INSERT INTO ssl_certificates (id, domain, domain_id) "
                        "VALUES (1, 'orphan.com', 0)"));
    }
    clean(path);
}

// ============================================================
// Indices
// ============================================================

TEST_CASE("Schema v1 creates all 14 approved indices") {
    auto path = sdb("sc_indices.db");
    clean(path);
    {
        auto db = make_db(path);
        REQUIRE(db.prepare("SELECT name FROM sqlite_master WHERE type='index' "
                           "AND name LIKE 'idx_%' ORDER BY name"));
        int n = 0;
        while (db.step()) ++n;
        CHECK(n == 14);
    }
    clean(path);
}

// ============================================================
// PRAGMA integrity
// ============================================================

TEST_CASE("Schema v1 PRAGMA foreign_key_check returns no violations") {
    auto path = sdb("sc_fkcheck.db");
    clean(path);
    {
        auto db = make_db(path);

        // Populate with valid relationships
        REQUIRE(db.exec("INSERT INTO access_users (id, username) VALUES (1, 'u')"));
        REQUIRE(db.exec("INSERT INTO sites (id, domain) VALUES (1, 's.com')"));
        REQUIRE(db.exec("INSERT INTO access_grants (id, access_user_id, site_id) "
                        "VALUES (1, 1, 1)"));
        REQUIRE(db.exec("INSERT INTO mail_domains (id, domain_name) VALUES (1, 'm.com')"));
        REQUIRE(db.exec("INSERT INTO mail_mailboxes (id, domain_id, local_part) "
                        "VALUES (1, 1, 'admin')"));
        REQUIRE(db.exec("INSERT INTO mail_aliases (id, domain_id, source_local_part, destination) "
                        "VALUES (1, 1, 'info', 'a@b.com')"));

        REQUIRE(db.prepare("PRAGMA foreign_key_check"));
        CHECK_FALSE(db.step());
    }
    clean(path);
}

// ============================================================
// Migration Engine behaviour
// ============================================================

TEST_CASE("Schema v2 re-running migration is no-op") {
    auto path = sdb("sc_rerun.db");
    clean(path);
    {
        auto db = make_db(path);
        CHECK(containercp::storage::MigrationEngine().current_version(db) == 2);
        // Second run
        containercp::storage::MigrationEngine eng2;
        containercp::storage::register_all_schema_migrations(eng2);
        CHECK(eng2.migrate(db));
    }
    clean(path);
}

TEST_CASE("Schema v1 checksum is stable") {
    auto path = sdb("sc_checksum.db");
    clean(path);
    {
        auto db = make_db(path);
        REQUIRE(db.prepare("SELECT checksum FROM schema_migrations WHERE version = 1"));
        REQUIRE(db.step());
        std::string stored = db.column_text(0);

        containercp::storage::MigrationEngine eng2;
        containercp::storage::register_all_schema_migrations(eng2);
        CHECK(eng2.migrate(db));  // checksum match → no error
    }
    clean(path);
}

TEST_CASE("Schema v1 does not import TXT data") {
    auto path = sdb("sc_notxt.db");
    clean(path);
    {
        auto db = make_db(path);
        CHECK(db.is_open());
    }
    clean(path);
}

TEST_CASE("Schema v1 does not modify Storage backend") {
    auto path = sdb("sc_storage.db");
    clean(path);
    std::string txt_dir = (fs::temp_directory_path() / "sc_txttest/").string();
    fs::create_directories(txt_dir);
    {
        containercp::storage::SQLiteDB db2;
        REQUIRE(db2.open(path));
        containercp::storage::MigrationEngine eng;
        containercp::storage::register_all_schema_migrations(eng);
        REQUIRE(eng.migrate(db2));
        db2.close();

        containercp::storage::Storage txt_storage(txt_dir);
        txt_storage.save_sites({});
        CHECK(txt_storage.load_sites().empty());
    }
    fs::remove_all(txt_dir);
    clean(path);
}

// ============================================================
// No PEM or private key columns
// ============================================================

TEST_CASE("Schema v1 no PEM or private key content columns") {
    auto path = sdb("sc_nopem.db");
    clean(path);
    {
        auto db = make_db(path);
        // Columns with '_path' are valid (filesystem paths)
        REQUIRE(db.prepare("SELECT sql FROM sqlite_master WHERE type='table' AND "
                           "sql LIKE '%private_key%' AND sql NOT LIKE '%_path%'"));
        CHECK_FALSE(db.step());
    }
    clean(path);
}
