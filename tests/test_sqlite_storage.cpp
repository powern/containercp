#include "storage/Storage.h"
#include "storage/SQLiteStorage.h"
#include "storage/SchemaMigrations.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "doctest/doctest.h"

namespace fs = std::filesystem;

static std::string tdir(const std::string& name) {
    return (fs::temp_directory_path() / name).string() + "/";
}

static void tclean(const std::string& dir) {
    fs::remove_all(dir);
}

// Helper: create a pool with schema migration for direct SQLiteStorage tests.
static void init_pool(containercp::storage::ConnectionPool& pool, const std::string& dir) {
    REQUIRE(pool.initialize(dir + "test.db"));
    containercp::storage::SQLiteDB migrator;
    REQUIRE(migrator.open(dir + "test.db"));
    containercp::storage::MigrationEngine eng;
    containercp::storage::register_all_schema_migrations(eng);
    REQUIRE(eng.migrate(migrator));
    migrator.close();
}

// ============================================================
// TransactionGuard tests
// ============================================================

TEST_CASE("TransactionGuard rolls back by default") {
    auto dir = tdir("tg_rollback");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);

        // Insert via TransactionGuard; do NOT call commit()
        {
            containercp::storage::TransactionGuard txn(pool);
            REQUIRE(txn.is_active());
            REQUIRE(pool.write_connection().exec("INSERT INTO mail_config VALUES ('k1', 'v1')"));
        }  // destructor rolls back

        // Verify rollback
        containercp::storage::ReadLease rl(pool);
        REQUIRE(rl.is_valid());
        REQUIRE(rl->prepare("SELECT COUNT(*) FROM mail_config WHERE key='k1'"));
        REQUIRE(rl->step());
        CHECK(rl->column_int(0) == 0);
    }
    tclean(dir);
}

TEST_CASE("TransactionGuard explicit commit persists") {
    auto dir = tdir("tg_commit");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);

        {
            containercp::storage::TransactionGuard txn(pool);
            REQUIRE(txn.is_active());
            REQUIRE(pool.write_connection().exec("INSERT INTO mail_config VALUES ('k1', 'v1')"));
            REQUIRE(txn.commit());
        }

        containercp::storage::ReadLease rl(pool);
        REQUIRE(rl.is_valid());
        REQUIRE(rl->prepare("SELECT COUNT(*) FROM mail_config WHERE key='k1'"));
        REQUIRE(rl->step());
        CHECK(rl->column_int(0) == 1);
    }
    tclean(dir);
}

TEST_CASE("TransactionGuard rollback by default") {
    auto dir = tdir("tg_rollback2");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);

        {
            containercp::storage::TransactionGuard txn(pool);
            REQUIRE(txn.is_active());
            REQUIRE(pool.write_connection().exec("INSERT INTO mail_config VALUES ('k1', 'v1')"));
            // No commit → destructor rolls back
        }

        containercp::storage::ReadLease rl(pool);
        REQUIRE(rl.is_valid());
        REQUIRE(rl->prepare("SELECT COUNT(*) FROM mail_config WHERE key='k1'"));
        REQUIRE(rl->step());
        CHECK(rl->column_int(0) == 0);
    }
    tclean(dir);
}

// TransactionGuard inactive-after-shutdown is not tested directly
// because TransactionGuard is always used with a live initialized pool.
// The rollback-by-default behavior is tested by the rollback test above.

// ============================================================
// SQLiteStorage direct tests (explicit SQLite mode)
// ============================================================

TEST_CASE("SQLiteStorage nodes empty load") {
    auto dir = tdir("ss_nodes_empty");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        CHECK(ss.load_nodes().empty());
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage nodes round trip") {
    auto dir = tdir("ss_nodes_rt");
    tclean(dir); fs::create_directories(dir);
    {
        auto pool = containercp::storage::ConnectionPool();
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        containercp::node::Node n;
        n.id = 1; n.name = "local"; n.type = "local";
        ss.save_nodes({n});
        auto loaded = ss.load_nodes();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].id == 1); CHECK(loaded[0].name == "local");
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage nodes preserve non-contiguous IDs") {
    auto dir = tdir("ss_ncid");
    tclean(dir); fs::create_directories(dir);
    {
        auto pool = containercp::storage::ConnectionPool();
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        containercp::node::Node n1, n2;
        n1.id = 1; n1.name = "first"; n1.type = "local";
        n2.id = 8; n2.name = "eighth"; n2.type = "local";
        ss.save_nodes({n1, n2});
        auto loaded = ss.load_nodes();
        REQUIRE(loaded.size() == 2);
        CHECK(loaded[0].id == 1); CHECK(loaded[1].id == 8);
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage nodes removal via replacement") {
    auto dir = tdir("ss_rem");
    tclean(dir); fs::create_directories(dir);
    {
        auto pool = containercp::storage::ConnectionPool();
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        containercp::node::Node n1, n2;
        n1.id = 1; n1.name = "keep"; n1.type = "local";
        n2.id = 2; n2.name = "remove"; n2.type = "local";
        ss.save_nodes({n1, n2});
        ss.save_nodes({n1});
        auto loaded = ss.load_nodes();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].id == 1);
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage php_versions round trip") {
    auto dir = tdir("ss_php_rt");
    tclean(dir); fs::create_directories(dir);
    {
        auto pool = containercp::storage::ConnectionPool();
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        containercp::php::PhpVersion pv;
        pv.id = 1; pv.version = "8.4"; pv.image = "php:8.4-fpm";
        pv.enabled = true; pv.default_version = true;
        ss.save_php_versions({pv});
        auto loaded = ss.load_php_versions();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].version == "8.4"); CHECK(loaded[0].default_version == true);
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage profiles round trip") {
    auto dir = tdir("ss_prof_rt");
    tclean(dir); fs::create_directories(dir);
    {
        auto pool = containercp::storage::ConnectionPool();
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        containercp::profile::Profile p;
        p.id = 1; p.profile_name = "test"; p.web_server = "nginx";
        p.template_path = "/etc/t/test.conf"; p.description = "Test";
        p.enabled = true; p.default_profile = true;
        ss.save_profiles({p});
        auto loaded = ss.load_profiles();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].profile_name == "test");
        CHECK(loaded[0].default_profile == true);
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage profiles special characters") {
    auto dir = tdir("ss_spec");
    tclean(dir); fs::create_directories(dir);
    {
        auto pool = containercp::storage::ConnectionPool();
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        containercp::profile::Profile p;
        p.id = 1; p.profile_name = "quote's & \"double\" \\pipe|unicode✓";
        p.web_server = "nginx"; p.description = "multi\nline";
        p.enabled = true; p.default_profile = false;
        ss.save_profiles({p});
        auto loaded = ss.load_profiles();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].profile_name == p.profile_name);
        CHECK(loaded[0].description == p.description);
    }
    tclean(dir);
}

// ============================================================
// Storage mode tests
// ============================================================

TEST_CASE("Storage default mode keeps nodes TXT-backed") {
    auto dir = tdir("ss_def_txt");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::Storage s(dir);  // default: CoreStorageBackend::Txt
        containercp::node::Node n;
        n.id = 1; n.name = "local"; n.type = "local";
        s.save_nodes({n});
        auto nodes = s.load_nodes();
        CHECK(nodes.size() == 1);
        // Verify TXT file was written
        CHECK(fs::exists(dir + "nodes.db"));
    }
    tclean(dir);
}

TEST_CASE("Storage default mode reads existing TXT nodes") {
    auto dir = tdir("ss_def_read");
    tclean(dir); fs::create_directories(dir);
    {
        // Write TXT node file manually
        std::ofstream f(dir + "nodes.db");
        f << "1|local|local\n";
    }
    {
        containercp::storage::Storage s(dir);  // default TXT mode
        auto nodes = s.load_nodes();
        REQUIRE(nodes.size() == 1);
        CHECK(nodes[0].id == 1);
        CHECK(nodes[0].name == "local");
    }
    tclean(dir);
}

TEST_CASE("Storage explicit SQLite mode") {
    auto dir = tdir("ss_exp_sqlite");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        containercp::storage::Storage s(dir, opts);
        containercp::node::Node n;
        n.id = 42; n.name = "sqlite-node"; n.type = "local";
        s.save_nodes({n});
        auto nodes = s.load_nodes();
        REQUIRE(nodes.size() == 1);
        CHECK(nodes[0].id == 42);

        // TXT file should NOT be created
        CHECK_FALSE(fs::exists(dir + "nodes.db"));
    }
    tclean(dir);
}

TEST_CASE("Storage explicit SQLite mode coexists with TXT resources") {
    auto dir = tdir("ss_exp_coexist");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        containercp::storage::Storage s(dir, opts);

        // Node → SQLite
        containercp::node::Node n;
        n.id = 1; n.name = "sqlite"; n.type = "local";
        s.save_nodes({n});

        // Auth user → TXT (unchanged)
        containercp::auth::AuthUser u;
        u.id = 1; u.username = "admin"; u.password_hash = "h"; u.role = "admin";
        s.save_auth_users({u});

        auto nodes = s.load_nodes();
        CHECK(nodes.size() == 1);
        auto auths = s.load_auth_users();
        CHECK(auths.size() == 1);
    }
    tclean(dir);
}

TEST_CASE("Storage default mode does not create containercp.db") {
    auto dir = tdir("ss_no_db");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::Storage s(dir);  // default TXT mode
        CHECK_FALSE(fs::exists(dir + "containercp.db"));
    }
    tclean(dir);
}

// ============================================================
// Safe write-access tests
// ============================================================

TEST_CASE("TransactionGuard on uninitialized pool does not crash") {
    containercp::storage::ConnectionPool pool;  // never initialized
    containercp::storage::TransactionGuard txn(pool);
    CHECK_FALSE(txn.is_active());
    // Mutex should be released (not locked after failed activation)
}

TEST_CASE("TransactionGuard on shut-down pool does not crash") {
    auto dir = tdir("tg_shutdown_pool");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        pool.shutdown();  // destroys write connection
        containercp::storage::TransactionGuard txn(pool);
        CHECK_FALSE(txn.is_active());
        // Mutex released — subsequent valid pool should work
    }
    tclean(dir);
}

TEST_CASE("TransactionGuard on shut-down pool then valid pool works") {
    auto dir = tdir("tg_recover");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        pool.shutdown();

        // TransactionGuard on shut-down pool — inactive
        {
            containercp::storage::TransactionGuard txn(pool);
            CHECK_FALSE(txn.is_active());
        }

        // Reinitialize — now TransactionGuard should work
        REQUIRE(pool.initialize(dir + "test.db"));
        {
            containercp::storage::TransactionGuard txn(pool);
            CHECK(txn.is_active());
            REQUIRE(txn.commit());
        }
    }
    tclean(dir);
}

// ============================================================
// Fail-closed explicit SQLite mode tests
// ============================================================

TEST_CASE("Explicit SQLite mode with failed init does not write to TXT") {
    auto dir = tdir("ss_fail_closed");
    tclean(dir); fs::create_directories(dir);
    {
        // Write a non-SQLite file at the path where containercp.db
        // will be created.  SQLite's open() succeeds, but PRAGMA
        // execution will fail because the file is not a valid
        // SQLite database.
        std::string db_file = dir + "containercp.db";
        {
            std::ofstream f(db_file);
            f << "not-a-valid-sqlite-database-file";
        }

        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;

        // Storage will try to open the existing file as a SQLite
        // database.  The file is not valid, so apply_pragmas or
        // schema migration will fail.
        containercp::storage::Storage s(dir, opts);
        CHECK_FALSE(s.sqlite_ready());

        // TXT files for core resources should NOT be created
        // in unavailable explicit mode
        CHECK_FALSE(fs::exists(dir + "nodes.db"));

        // Saving core resources should be no-ops (no crash)
        containercp::node::Node n;
        n.id = 1; n.name = "n"; n.type = "local";
        s.save_nodes({n});  // no-op, no crash
    }
    tclean(dir);
}

TEST_CASE("Explicit SQLite mode sqlite_ready reflects init status") {
    auto dir = tdir("ss_ready");
    tclean(dir); fs::create_directories(dir);
    // Default mode — not ready
    {
        containercp::storage::Storage s(dir);
        CHECK_FALSE(s.sqlite_ready());
    }
    tclean(dir);
}

TEST_CASE("sqlite_ready true when explicit mode init succeeds") {
    auto dir = tdir("ss_ready_ok");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());
    }
    tclean(dir);
}

// ============================================================
// Shutdown/write synchronization tests
// ============================================================

TEST_CASE("WriteGuard prevents shutdown completion") {
    auto dir = tdir("wg_shutdown_lock");
    tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool;
    init_pool(pool, dir);

    std::thread shutdown_thread;

    // Scope limits WriteGuard lifetime so it is released before join
    {
        containercp::storage::WriteGuard wg(pool);
        REQUIRE(wg.is_valid());

        shutdown_thread = std::thread([&] { pool.shutdown(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        CHECK(wg.db().exec("SELECT 1"));
    }  // wg destroyed → mutex released → shutdown proceeds

    shutdown_thread.join();
    tclean(dir);
}

TEST_CASE("WriteGuard cannot activate after shutdown starts") {
    auto dir = tdir("wg_no_activate");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);

        pool.shutdown();

        // After shutdown, WriteGuard should be invalid
        containercp::storage::WriteGuard wg(pool);
        CHECK_FALSE(wg.is_valid());
    }
    tclean(dir);
}

TEST_CASE("TransactionGuard prevents shutdown completion") {
    auto dir = tdir("tg_shutdown_lock");
    tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool;
    init_pool(pool, dir);

    std::thread shutdown_thread;

    {
        containercp::storage::TransactionGuard txn(pool);
        REQUIRE(txn.is_active());

        shutdown_thread = std::thread([&] { pool.shutdown(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        CHECK(txn.db().exec("SELECT 1"));
        CHECK(txn.commit());
    }  // txn destroyed → mutex released → shutdown proceeds

    shutdown_thread.join();
    tclean(dir);
}

TEST_CASE("TransactionGuard rollback then shutdown") {
    auto dir = tdir("tg_rollback_shutdown");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);

        {
            containercp::storage::TransactionGuard txn(pool);
            REQUIRE(txn.is_active());
            REQUIRE(txn.db().exec("INSERT INTO mail_config VALUES ('k', 'v')"));
        }

        pool.shutdown();
        CHECK(pool.is_shutdown());
    }
    tclean(dir);
}

TEST_CASE("TransactionGuard cannot activate after shutdown starts") {
    auto dir = tdir("tg_no_activate");
    tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool;
    init_pool(pool, dir);
    pool.shutdown();

    containercp::storage::TransactionGuard txn(pool);
    CHECK_FALSE(txn.is_active());
    tclean(dir);
}

TEST_CASE("Backup and shutdown do not race") {
    auto dir = tdir("bk_shutdown");
    tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool;
    init_pool(pool, dir);

    std::string backup_path = dir + "backup.db";
    std::thread backup_thread([&] { pool.backup(backup_path); });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // shutdown will block until backup's WriteGuard completes
    // (backup holds WriteGuard → lock_write → shutdown waits)
    // Then backup's WriteGuard releases → shutdown acquires lock →
    // destroys write_conn_ → backup's WriteGuard was already
    // destroyed so the DB handle is gone after shutdown.
    // This test verifies no crash or deadlock.
    pool.shutdown();
    backup_thread.join();
    tclean(dir);
}

TEST_CASE("Shutdown then reinitialize still works") {
    auto dir = tdir("shutdown_reinit");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        pool.shutdown();

        CHECK(pool.initialize(dir + "test.db"));

        containercp::storage::WriteGuard wg(pool);
        CHECK(wg.is_valid());
        CHECK(wg.db().exec("SELECT 1"));
    }
    tclean(dir);
}
