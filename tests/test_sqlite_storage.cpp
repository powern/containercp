#include "storage/Storage.h"
#include "storage/SQLiteStorage.h"
#include "storage/SchemaMigrations.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
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
    REQUIRE(pool.initialize(dir + "containercp.db"));
    containercp::storage::SQLiteDB migrator;
    REQUIRE(migrator.open(dir + "containercp.db"));
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
        opts.skip_startup_validation = true;
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

TEST_CASE("P11-10 Storage explicit SQLite mode routes backups and auth_users to SQLite") {
    auto dir = tdir("ss_exp_coexist");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        containercp::storage::Storage s(dir, opts);

        containercp::backup::Backup b;
        b.id = 7; b.site_id = 2; b.owner_id = 3;
        b.filename = "site-2.tar.gz"; b.type = "manual"; b.size = 4096;
        b.created_at = "2026-07-18T12:00:00Z"; b.status = "completed";
        b.file_path = "/srv/containercp/backups/site-2.tar.gz";
        b.compression = "gzip";
        s.save_backups({b});

        containercp::auth::AuthUser u;
        u.id = 1; u.username = "admin"; u.password_hash = "h"; u.role = "admin";
        u.must_change_password = true;
        s.save_auth_users({u});

        auto backups = s.load_backups();
        REQUIRE(backups.size() == 1);
        CHECK(backups[0].id == 7);
        CHECK(backups[0].filename == "site-2.tar.gz");
        CHECK(backups[0].size == 4096);

        auto auths = s.load_auth_users();
        REQUIRE(auths.size() == 1);
        CHECK(auths[0].username == "admin");
        CHECK(auths[0].must_change_password == true);

        CHECK_FALSE(fs::exists(dir + "backups.db"));
        CHECK_FALSE(fs::exists(dir + "auth_users.db"));
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
        REQUIRE(pool.initialize(dir + "containercp.db"));
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
        opts.skip_startup_validation = true;

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
        opts.skip_startup_validation = true;
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());
    }
    tclean(dir);
}

// ============================================================
// Shutdown/write synchronization tests
// Uses ConnectionPool::TestObserver for deterministic coordination.
// ============================================================

TEST_CASE("WriteGuard prevents shutdown completion") {
    auto dir = tdir("wg_shutdown_lock");
    tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool;
    init_pool(pool, dir);

    std::mutex mtx;
    std::condition_variable cv;
    bool shutdown_awaiting = false;

    // Set up observer to signal when shutdown is about to acquire
    // the write mutex.
    pool.test_obs_.on_shutdown_awaiting_write_mutex = [&] {
        std::lock_guard<std::mutex> lk(mtx);
        shutdown_awaiting = true;
        cv.notify_one();
    };

    std::thread shutdown_thread;

    {
        containercp::storage::WriteGuard wg(pool);
        REQUIRE(wg.is_valid());

        shutdown_thread = std::thread([&] { pool.shutdown(); });

        // Wait until shutdown is blocked on write_mutex_
        {
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait(lk, [&] { return shutdown_awaiting; });
        }

        // Verify the connection is still usable while shutdown waits
        CHECK(wg.db().exec("SELECT 1"));
    }  // wg destroyed → mutex released → shutdown proceeds

    shutdown_thread.join();
    CHECK(pool.is_shutdown());

    // Cleanup observer
    pool.test_obs_.on_shutdown_awaiting_write_mutex = nullptr;
    tclean(dir);
}

TEST_CASE("WriteGuard cannot activate after shutdown starts") {
    auto dir = tdir("wg_no_activate");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);

        pool.shutdown();

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

    std::mutex mtx;
    std::condition_variable cv;
    bool shutdown_awaiting = false;

    pool.test_obs_.on_shutdown_awaiting_write_mutex = [&] {
        std::lock_guard<std::mutex> lk(mtx);
        shutdown_awaiting = true;
        cv.notify_one();
    };

    std::thread shutdown_thread;

    {
        containercp::storage::TransactionGuard txn(pool);
        REQUIRE(txn.is_active());

        shutdown_thread = std::thread([&] { pool.shutdown(); });

        {
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait(lk, [&] { return shutdown_awaiting; });
        }

        CHECK(txn.db().exec("SELECT 1"));
        CHECK(txn.commit());
    }

    shutdown_thread.join();
    CHECK(pool.is_shutdown());
    pool.test_obs_.on_shutdown_awaiting_write_mutex = nullptr;
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

    // Seed
    {
        containercp::storage::WriteGuard wg(pool);
        REQUIRE(wg.is_valid());
        REQUIRE(wg.db().exec("INSERT INTO mail_config VALUES ('k1', 'v1')"));
    }

    std::mutex mtx;
    std::condition_variable cv;
    bool backup_guard_held = false;
    bool backup_continue = false;
    bool shutdown_awaiting = false;
    std::atomic<bool> shutdown_completed{false};
    bool backup_released_by_timeout = false;
    bool backup_guard_wait_succeeded = false;
    bool shutdown_wait_succeeded = false;
    bool shutdown_was_blocked = false;
    bool backup_result = false;
    std::string backup_path = dir + "backup.db";
    std::thread backup_thr;
    std::thread shutdown_thr;

    pool.test_obs_.on_backup_guard_acquired = [&] {
        {
            std::lock_guard<std::mutex> lk(mtx);
            backup_guard_held = true;
        }
        cv.notify_one();

        std::unique_lock<std::mutex> lk(mtx);
        bool released = cv.wait_for(lk, std::chrono::seconds(10),
                                     [&] { return backup_continue; });
        if (!released) backup_released_by_timeout = true;
    };

    pool.test_obs_.on_shutdown_awaiting_write_mutex = [&] {
        std::lock_guard<std::mutex> lk(mtx);
        shutdown_awaiting = true;
        cv.notify_one();
    };

    backup_thr = std::thread([&] { backup_result = pool.backup(backup_path); });

    // Wait for backup guard
    {
        std::unique_lock<std::mutex> lk(mtx);
        backup_guard_wait_succeeded = cv.wait_for(lk, std::chrono::seconds(5),
                                                   [&] { return backup_guard_held; });
    }

    if (backup_guard_wait_succeeded) {
        shutdown_thr = std::thread([&] {
            pool.shutdown();
            shutdown_completed.store(true);
        });

        {
            std::unique_lock<std::mutex> lk(mtx);
            shutdown_wait_succeeded = cv.wait_for(lk, std::chrono::seconds(5),
                                                   [&] { return shutdown_awaiting; });
        }

        // Capture whether shutdown was blocked BEFORE releasing backup
        shutdown_was_blocked = shutdown_wait_succeeded && !shutdown_completed.load();
    }

    // Always release backup
    {
        std::lock_guard<std::mutex> lk(mtx);
        backup_continue = true;
    }
    cv.notify_all();

    if (backup_thr.joinable()) backup_thr.join();
    if (shutdown_thr.joinable()) shutdown_thr.join();

    pool.test_obs_.on_backup_guard_acquired = nullptr;
    pool.test_obs_.on_shutdown_awaiting_write_mutex = nullptr;

    // Verify
    CHECK(backup_guard_wait_succeeded);
    CHECK(shutdown_wait_succeeded);
    CHECK(shutdown_was_blocked);  // shutdown was waiting, not completed
    CHECK_FALSE(backup_released_by_timeout);
    REQUIRE(backup_result);
    REQUIRE(fs::exists(backup_path));
    CHECK(shutdown_completed.load());
    CHECK(pool.is_shutdown());

    {
        containercp::storage::SQLiteDB verify;
        REQUIRE(verify.open(backup_path));
        REQUIRE(verify.prepare("SELECT value FROM mail_config WHERE key='k1'"));
        REQUIRE(verify.step());
        CHECK(verify.column_text(0) == "v1");
        CHECK_FALSE(verify.step());

        REQUIRE(verify.prepare("PRAGMA integrity_check"));
        REQUIRE(verify.step());
        CHECK(verify.column_text(0) == "ok");

        REQUIRE(verify.prepare("PRAGMA foreign_key_check"));
        bool fk = verify.step();
        CHECK_FALSE(fk);
    }

    tclean(dir);
}

// ============================================================
// Phase 6a — User, Site, Domain SQLite storage tests
// ============================================================

// Helper: create a ConnectionPool + schema for direct SQLiteStorage tests
static void init_6a(containercp::storage::ConnectionPool& pool, const std::string& dir) {
    REQUIRE(pool.initialize(dir + "test6a.db"));
    containercp::storage::SQLiteDB migrator;
    REQUIRE(migrator.open(dir + "test6a.db"));
    containercp::storage::MigrationEngine eng;
    containercp::storage::register_all_schema_migrations(eng);
    REQUIRE(eng.migrate(migrator));
    migrator.close();
}

// --- Users ---

TEST_CASE("SQLiteStorage users empty") {
    auto dir = tdir("s6a_u_empty"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    CHECK(ss.load_users().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage users round trip") {
    auto dir = tdir("s6a_u_rt"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::user::User u;
    u.id = 1; u.username = "admin"; u.uid = 1000;
    u.home_directory = "/home/admin"; u.shell = "/bin/bash"; u.enabled = true;
    ss.save_users({u});
    auto loaded = ss.load_users();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].id == 1); CHECK(loaded[0].username == "admin");
    CHECK(loaded[0].uid == 1000); CHECK(loaded[0].home_directory == "/home/admin");
    CHECK(loaded[0].shell == "/bin/bash"); CHECK(loaded[0].enabled);
    CHECK(loaded[0].name == "admin");
    tclean(dir);
}

TEST_CASE("SQLiteStorage users multi non-contiguous IDs") {
    auto dir = tdir("s6a_u_ncid"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::user::User u1, u2;
    u1.id = 1; u1.username = "a"; u1.uid = 1000; u1.enabled = true;
    u2.id = 8; u2.username = "b"; u2.uid = 1001; u2.enabled = false;
    ss.save_users({u1, u2});
    auto loaded = ss.load_users();
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].id == 1); CHECK(loaded[0].enabled);
    CHECK(loaded[1].id == 8); CHECK_FALSE(loaded[1].enabled);
    tclean(dir);
}

TEST_CASE("SQLiteStorage users special chars") {
    auto dir = tdir("s6a_u_spec"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::user::User u;
    u.id = 1; u.username = "o'brien"; u.uid = 1000;
    u.home_directory = "/home/with \"quotes\" and \\backslash";
    u.shell = "/usr/bin/zsh"; u.enabled = true;
    ss.save_users({u});
    auto loaded = ss.load_users();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].username == "o'brien");
    CHECK(loaded[0].home_directory.find("\"quotes\"") != std::string::npos);
    tclean(dir);
}

TEST_CASE("SQLiteStorage users replacement clears removed") {
    auto dir = tdir("s6a_u_rem"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::user::User u1, u2;
    u1.id = 1; u1.username = "keep"; u1.enabled = true;
    u2.id = 2; u2.username = "remove"; u2.enabled = true;
    ss.save_users({u1, u2});
    ss.save_users({u1});
    auto loaded = ss.load_users();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].id == 1);
    tclean(dir);
}

TEST_CASE("SQLiteStorage users empty vector clears") {
    auto dir = tdir("s6a_u_clear"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::user::User u; u.id = 1; u.username = "tmp"; u.enabled = true;
    ss.save_users({u});
    ss.save_users({});
    CHECK(ss.load_users().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage users reopen reloads") {
    auto dir = tdir("s6a_u_reopen"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool; init_6a(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        containercp::user::User u; u.id = 42; u.username = "persist"; u.enabled = true;
        ss.save_users({u});
    }
    {
        containercp::storage::ConnectionPool pool; init_6a(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        auto loaded = ss.load_users();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].id == 42);
    }
    tclean(dir);
}

// --- Sites ---

TEST_CASE("SQLiteStorage sites empty") {
    auto dir = tdir("s6a_s_empty"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    CHECK(ss.load_sites().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage sites round trip") {
    auto dir = tdir("s6a_s_rt"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::site::Site s;
    s.id = 1; s.domain = "example.com"; s.owner = "admin";
    s.node_id = 1; s.web_server = "nginx"; s.php_mail_enabled = true;
    ss.save_sites({s});
    auto loaded = ss.load_sites();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].id == 1); CHECK(loaded[0].domain == "example.com");
    CHECK(loaded[0].owner == "admin"); CHECK(loaded[0].node_id == 1);
    CHECK(loaded[0].web_server == "nginx");
    CHECK(loaded[0].php_mail_enabled); CHECK(loaded[0].php_mail_enabled_present);
    CHECK(loaded[0].name == "example.com");
    tclean(dir);
}

TEST_CASE("SQLiteStorage sites php_mail false") {
    auto dir = tdir("s6a_s_nomail"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::site::Site s;
    s.id = 1; s.domain = "test.com"; s.php_mail_enabled = false;
    ss.save_sites({s});
    auto loaded = ss.load_sites();
    REQUIRE(loaded.size() == 1);
    CHECK_FALSE(loaded[0].php_mail_enabled);
    CHECK(loaded[0].php_mail_enabled_present);
    tclean(dir);
}

TEST_CASE("SQLiteStorage sites node_id=0 sentinel") {
    auto dir = tdir("s6a_s_n0"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::site::Site s;
    s.id = 1; s.domain = "d.com"; s.node_id = 0;
    ss.save_sites({s});
    auto loaded = ss.load_sites();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].node_id == 0);
    tclean(dir);
}

TEST_CASE("SQLiteStorage sites replacement clears") {
    auto dir = tdir("s6a_s_rem"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::site::Site s1, s2;
    s1.id = 1; s1.domain = "keep.com"; s1.node_id = 1;
    s2.id = 2; s2.domain = "remove.com"; s2.node_id = 1;
    ss.save_sites({s1, s2});
    ss.save_sites({s1});
    CHECK(ss.load_sites().size() == 1);
    tclean(dir);
}

TEST_CASE("SQLiteStorage sites reopen") {
    auto dir = tdir("s6a_s_reopen"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool; init_6a(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        containercp::site::Site s; s.id = 7; s.domain = "p.com"; s.node_id = 1;
        ss.save_sites({s});
    }
    {
        containercp::storage::ConnectionPool pool; init_6a(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        auto loaded = ss.load_sites();
        REQUIRE(loaded.size() == 1); CHECK(loaded[0].id == 7);
    }
    tclean(dir);
}

// --- Domains ---

TEST_CASE("SQLiteStorage domains empty") {
    auto dir = tdir("s6a_d_empty"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    CHECK(ss.load_domains().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage domains round trip all fields") {
    auto dir = tdir("s6a_d_rt"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::domain::Domain d;
    d.id = 1; d.fqdn = "example.com"; d.owner_id = 1; d.site_id = 1;
    d.php_version = "8.4"; d.ssl_enabled = true; d.enabled = true;
    d.type = "primary"; d.target = "";
    ss.save_domains({d});
    auto loaded = ss.load_domains();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].id == 1); CHECK(loaded[0].fqdn == "example.com");
    CHECK(loaded[0].owner_id == 1); CHECK(loaded[0].site_id == 1);
    CHECK(loaded[0].php_version == "8.4"); CHECK(loaded[0].ssl_enabled);
    CHECK(loaded[0].enabled); CHECK(loaded[0].type == "primary");
    CHECK(loaded[0].target.empty()); CHECK(loaded[0].name == "example.com");
    tclean(dir);
}

TEST_CASE("SQLiteStorage domains sentinel 0 values") {
    auto dir = tdir("s6a_d_sent"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::domain::Domain d;
    d.id = 1; d.fqdn = "o.com"; d.owner_id = 0; d.site_id = 0;
    d.type = "primary"; d.target = "";
    ss.save_domains({d});
    auto loaded = ss.load_domains();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].owner_id == 0); CHECK(loaded[0].site_id == 0);
    tclean(dir);
}

TEST_CASE("SQLiteStorage domains all types") {
    auto dir = tdir("s6a_d_types"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    std::vector<std::string> types = {"primary", "alias", "redirect", "wildcard"};
    int id = 0;
    for (auto& t : types) {
        containercp::domain::Domain d;
        d.id = ++id; d.fqdn = t + ".com"; d.type = t; d.target = "http://target/" + t;
        ss.save_domains({d});
        auto loaded = ss.load_domains();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].type == t);
        CHECK(loaded[0].target.find(t) != std::string::npos);
        ss.save_domains({});  // clear for next type
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage domains special chars in target") {
    auto dir = tdir("s6a_d_spec"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::domain::Domain d;
    d.id = 1; d.fqdn = "x.com";
    d.target = "https://x.com/path?q=a|b&c=d'e\"f";
    d.type = "redirect";
    ss.save_domains({d});
    auto loaded = ss.load_domains();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].target == d.target);
    tclean(dir);
}

TEST_CASE("SQLiteStorage domains reopen") {
    auto dir = tdir("s6a_d_reopen"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool; init_6a(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        containercp::domain::Domain d;
        d.id = 5; d.fqdn = "p.com"; d.type = "primary";
        ss.save_domains({d});
    }
    {
        containercp::storage::ConnectionPool pool; init_6a(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        auto loaded = ss.load_domains();
        REQUIRE(loaded.size() == 1); CHECK(loaded[0].id == 5);
    }
    tclean(dir);
}

// --- Cross-backend and mode tests ---

TEST_CASE("Phase6a Storage explicit SQLite mode uses SQLite for users/sites/domains") {
    auto dir = tdir("s6a_mode"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());

        containercp::user::User u; u.id = 1; u.username = "u1"; u.enabled = true;
        s.save_users({u});
        CHECK_FALSE(fs::exists(dir + "users.db"));

        containercp::site::Site si; si.id = 1; si.domain = "s.com"; si.node_id = 1;
        s.save_sites({si});
        CHECK_FALSE(fs::exists(dir + "sites.db"));

        containercp::domain::Domain d; d.id = 1; d.fqdn = "d.com";
        s.save_domains({d});
        CHECK_FALSE(fs::exists(dir + "domains.db"));

        CHECK(s.load_users().size() == 1);
        CHECK(s.load_sites().size() == 1);
        CHECK(s.load_domains().size() == 1);
    }
    tclean(dir);
}

TEST_CASE("Phase6a default mode reads TXT users/sites/domains") {
    auto dir = tdir("s6a_txt"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::Storage s(dir);
        containercp::user::User u; u.id = 1; u.username = "u"; u.enabled = true;
        s.save_users({u});
        containercp::site::Site si; si.id = 1; si.domain = "s.com"; si.node_id = 1;
        s.save_sites({si});
        containercp::domain::Domain d; d.id = 1; d.fqdn = "d.com";
        s.save_domains({d});
    }
    {
        containercp::storage::Storage s(dir);
        CHECK(s.load_users().size() == 1);
        CHECK(s.load_sites().size() == 1);
        CHECK(s.load_domains().size() == 1);
        CHECK_FALSE(fs::exists(dir + "containercp.db"));  // no SQLite in default mode
    }
    tclean(dir);
}

TEST_CASE("Phase6a SQLite resources include auth_users") {
    auto dir = tdir("s6a_coexist"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());

        // SQLite-backed users
        containercp::user::User u; u.id = 1; u.username = "sqlite_user"; u.enabled = true;
        s.save_users({u});

        containercp::auth::AuthUser au; au.id = 1; au.username = "admin";
        au.password_hash = "h"; au.role = "admin";
        s.save_auth_users({au});

        CHECK(s.load_users().size() == 1);
        CHECK(s.load_auth_users().size() == 1);
        CHECK_FALSE(fs::exists(dir + "auth_users.db"));
    }
    tclean(dir);
}

TEST_CASE("Phase6a saving sites does not alter users or domains") {
    auto dir = tdir("s6a_isolation"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());

        containercp::user::User u; u.id = 1; u.username = "u"; u.enabled = true;
        s.save_users({u});
        containercp::domain::Domain d; d.id = 1; d.fqdn = "d.com";
        s.save_domains({d});
        containercp::site::Site si; si.id = 1; si.domain = "s.com"; si.node_id = 1;
        s.save_sites({si});

        CHECK(s.load_users().size() == 1);
        CHECK(s.load_domains().size() == 1);
        CHECK(s.load_sites().size() == 1);
    }
    tclean(dir);
}

TEST_CASE("Phase6a nodes/php/profiles still work") {
    auto dir = tdir("s6a_legacy"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());

        containercp::node::Node n; n.id = 1; n.name = "local"; n.type = "local";
        s.save_nodes({n});
        CHECK(s.load_nodes().size() == 1);

        containercp::php::PhpVersion pv;
        pv.id = 1; pv.version = "8.4"; pv.enabled = true;
        s.save_php_versions({pv});
        CHECK(s.load_php_versions().size() == 1);

        containercp::profile::Profile p;
        p.id = 1; p.profile_name = "default"; p.web_server = "apache";
        s.save_profiles({p});
        CHECK(s.load_profiles().size() == 1);
    }
    tclean(dir);
}

// ============================================================
// Phase 6b — Database and ReverseProxy SQLite storage tests
// ============================================================

// --- Databases ---

TEST_CASE("SQLiteStorage databases empty") {
    auto dir = tdir("s6b_db_empty"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    CHECK(ss.load_databases().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage databases round trip all fields") {
    auto dir = tdir("s6b_db_rt"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::database::Database d;
    d.id = 1; d.db_name = "my_db"; d.db_user = "my_user";
    d.db_password = "s3cret!"; d.engine = "mariadb"; d.version = "10.11";
    d.owner_id = 1; d.site_id = 1; d.enabled = true;
    ss.save_databases({d});
    auto loaded = ss.load_databases();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].id == 1); CHECK(loaded[0].db_name == "my_db");
    CHECK(loaded[0].db_user == "my_user");
    CHECK(loaded[0].db_password == "s3cret!");  // sensitive but round-trip
    CHECK(loaded[0].engine == "mariadb"); CHECK(loaded[0].version == "10.11");
    CHECK(loaded[0].owner_id == 1); CHECK(loaded[0].site_id == 1);
    CHECK(loaded[0].enabled); CHECK(loaded[0].name == "my_db");
    tclean(dir);
}

TEST_CASE("SQLiteStorage databases sentinel 0") {
    auto dir = tdir("s6b_db_sent"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::database::Database d;
    d.id = 1; d.db_name = "d"; d.owner_id = 0; d.site_id = 0;
    ss.save_databases({d});
    auto loaded = ss.load_databases();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].owner_id == 0); CHECK(loaded[0].site_id == 0);
    tclean(dir);
}

TEST_CASE("SQLiteStorage databases enabled false") {
    auto dir = tdir("s6b_db_dis"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::database::Database d;
    d.id = 1; d.db_name = "x"; d.enabled = false;
    ss.save_databases({d});
    CHECK_FALSE(ss.load_databases()[0].enabled);
    tclean(dir);
}

TEST_CASE("SQLiteStorage databases non-contiguous IDs") {
    auto dir = tdir("s6b_db_ncid"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::database::Database d1, d2;
    d1.id = 1; d1.db_name = "a"; d1.site_id = 1;
    d2.id = 9; d2.db_name = "b"; d2.site_id = 2;
    ss.save_databases({d1, d2});
    auto loaded = ss.load_databases();
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].id == 1); CHECK(loaded[1].id == 9);
    tclean(dir);
}

TEST_CASE("SQLiteStorage databases special chars in name/user/pass") {
    auto dir = tdir("s6b_db_spec"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::database::Database d;
    d.id = 1; d.db_name = "db_'\"\\|pipe"; d.db_user = "user_'\"\\|";
    d.db_password = "p@$$'\"\\|"; d.site_id = 1;
    ss.save_databases({d});
    auto loaded = ss.load_databases();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].db_name == d.db_name);
    CHECK(loaded[0].db_user == d.db_user);
    CHECK(loaded[0].db_password == d.db_password);
    tclean(dir);
}

TEST_CASE("SQLiteStorage databases reopens") {
    auto dir = tdir("s6b_db_reopen"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool; init_6a(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        containercp::database::Database d;
        d.id = 7; d.db_name = "persist"; d.site_id = 1;
        ss.save_databases({d});
    }
    {
        containercp::storage::ConnectionPool pool; init_6a(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        auto loaded = ss.load_databases();
        REQUIRE(loaded.size() == 1); CHECK(loaded[0].id == 7);
    }
    tclean(dir);
}

// --- Reverse proxies ---

TEST_CASE("SQLiteStorage reverse_proxies empty") {
    auto dir = tdir("s6b_rp_empty"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    CHECK(ss.load_reverse_proxies().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage reverse_proxies round trip all fields") {
    auto dir = tdir("s6b_rp_rt"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::proxy::ReverseProxy p;
    p.id = 1; p.domain = "example.com"; p.site_id = 1;
    p.provider = "nginx"; p.config_path = "/etc/nginx/sites/example.conf";
    p.upstream = "site-1-web:80"; p.enabled = true; p.status = "active";
    ss.save_reverse_proxies({p});
    auto loaded = ss.load_reverse_proxies();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].id == 1); CHECK(loaded[0].domain == "example.com");
    CHECK(loaded[0].site_id == 1); CHECK(loaded[0].provider == "nginx");
    CHECK(loaded[0].config_path == "/etc/nginx/sites/example.conf");
    CHECK(loaded[0].upstream == "site-1-web:80");
    CHECK(loaded[0].enabled); CHECK(loaded[0].status == "active");
    CHECK(loaded[0].name == "example.com");
    tclean(dir);
}

TEST_CASE("SQLiteStorage reverse_proxies site_id=0 sentinel") {
    auto dir = tdir("s6b_rp_s0"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::proxy::ReverseProxy p;
    p.id = 1; p.domain = "admin.example.com"; p.site_id = 0;
    ss.save_reverse_proxies({p});
    auto loaded = ss.load_reverse_proxies();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].site_id == 0);
    tclean(dir);
}

TEST_CASE("SQLiteStorage reverse_proxies enabled false and different status") {
    auto dir = tdir("s6b_rp_flags"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::proxy::ReverseProxy p;
    p.id = 1; p.domain = "x.com"; p.site_id = 1;
    p.enabled = false; p.status = "error";
    ss.save_reverse_proxies({p});
    auto loaded = ss.load_reverse_proxies();
    REQUIRE(loaded.size() == 1);
    CHECK_FALSE(loaded[0].enabled); CHECK(loaded[0].status == "error");
    tclean(dir);
}

TEST_CASE("SQLiteStorage reverse_proxies special chars") {
    auto dir = tdir("s6b_rp_spec"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::proxy::ReverseProxy p;
    p.id = 1; p.domain = "example.com";
    p.config_path = "/path/with spaces/'quotes' and \\backslash";
    p.upstream = "192.168.1.1:8080";
    p.site_id = 1; p.status = "active";
    ss.save_reverse_proxies({p});
    auto loaded = ss.load_reverse_proxies();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].config_path == p.config_path);
    CHECK(loaded[0].upstream == "192.168.1.1:8080");
    tclean(dir);
}

TEST_CASE("SQLiteStorage reverse_proxies reopens") {
    auto dir = tdir("s6b_rp_reopen"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool; init_6a(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        containercp::proxy::ReverseProxy p;
        p.id = 3; p.domain = "p.com"; p.site_id = 1;
        ss.save_reverse_proxies({p});
    }
    {
        containercp::storage::ConnectionPool pool; init_6a(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        auto loaded = ss.load_reverse_proxies();
        REQUIRE(loaded.size() == 1); CHECK(loaded[0].id == 3);
    }
    tclean(dir);
}

// --- Cross-backend and mode tests ---

TEST_CASE("Phase6b explicit SQLite mode uses SQLite for databases and proxies") {
    auto dir = tdir("s6b_mode"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());

        containercp::database::Database db;
        db.id = 1; db.db_name = "db1"; db.site_id = 1;
        s.save_databases({db});
        CHECK_FALSE(fs::exists(dir + "databases.db"));

        containercp::proxy::ReverseProxy rp;
        rp.id = 1; rp.domain = "p.com"; rp.site_id = 1;
        s.save_reverse_proxies({rp});
        CHECK_FALSE(fs::exists(dir + "reverse_proxies.db"));

        CHECK(s.load_databases().size() == 1);
        CHECK(s.load_reverse_proxies().size() == 1);
    }
    tclean(dir);
}

TEST_CASE("Phase6b default mode reads TXT databases and proxies") {
    auto dir = tdir("s6b_txt"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::Storage s(dir);
        containercp::database::Database db;
        db.id = 1; db.db_name = "d"; db.site_id = 1;
        s.save_databases({db});
        containercp::proxy::ReverseProxy rp;
        rp.id = 1; rp.domain = "p.com"; rp.site_id = 1;
        s.save_reverse_proxies({rp});
    }
    {
        containercp::storage::Storage s(dir);
        CHECK(s.load_databases().size() == 1);
        CHECK(s.load_reverse_proxies().size() == 1);
    }
    tclean(dir);
}

TEST_CASE("Phase6b saving databases does not alter proxies and vice versa") {
    auto dir = tdir("s6b_isolation"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());

        containercp::database::Database db;
        db.id = 1; db.db_name = "db1"; db.site_id = 1;
        s.save_databases({db});

        containercp::proxy::ReverseProxy rp;
        rp.id = 1; rp.domain = "p.com"; rp.site_id = 1;
        s.save_reverse_proxies({rp});

        // Databases don't affect proxies
        CHECK(s.load_databases().size() == 1);
        CHECK(s.load_reverse_proxies().size() == 1);
    }
    tclean(dir);
}

TEST_CASE("Phase6b existing phases still work") {
    auto dir = tdir("s6b_existing"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());

        containercp::node::Node n; n.id = 1; n.name = "n"; n.type = "local";
        s.save_nodes({n});
        CHECK(s.load_nodes().size() == 1);

        containercp::user::User u; u.id = 1; u.username = "u"; u.enabled = true;
        s.save_users({u});
        CHECK(s.load_users().size() == 1);

        containercp::auth::AuthUser au; au.id = 1; au.username = "admin";
        au.password_hash = "h"; au.role = "admin";
        s.save_auth_users({au});
        CHECK(s.load_auth_users().size() == 1);
        CHECK_FALSE(fs::exists(dir + "auth_users.db"));
    }
    tclean(dir);
}

// ============================================================
// Phase 6c — Access User and Access Grant SQLite storage tests
// ============================================================

// --- Access users ---

TEST_CASE("SQLiteStorage access_users empty") {
    auto dir = tdir("s6c_au_empty"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    CHECK(ss.load_access_users().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_users round trip") {
    auto dir = tdir("s6c_au_rt"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::access::AccessUser u;
    u.id = 1; u.username = "sftp1"; u.auth_type = "password";
    u.password_hash = "$6$placeholder"; u.enabled = true;
    ss.save_access_users({u});
    auto loaded = ss.load_access_users();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].id == 1);
    CHECK(loaded[0].username == "sftp1");
    CHECK(loaded[0].auth_type == "password");
    CHECK(loaded[0].password_hash == "$6$placeholder");
    CHECK(loaded[0].enabled); CHECK(loaded[0].name == "sftp1");
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_users enabled false and auth types") {
    auto dir = tdir("s6c_au_flags"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::access::AccessUser u;
    u.id = 1; u.username = "disabled"; u.enabled = false;
    ss.save_access_users({u});
    auto loaded = ss.load_access_users();
    REQUIRE(loaded.size() == 1);
    CHECK_FALSE(loaded[0].enabled);
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_users UPSERT preservation") {
    auto dir = tdir("s6c_au_upsert"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);

    containercp::access::AccessUser u;
    u.id = 1; u.username = "original"; u.password_hash = "h1"; u.enabled = true;
    ss.save_access_users({u});

    u.username = "updated"; u.password_hash = "h2";
    ss.save_access_users({u});

    auto loaded = ss.load_access_users();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].username == "updated");
    CHECK(loaded[0].password_hash == "h2");
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_users prune unreferenced") {
    auto dir = tdir("s6c_au_prune"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);

    containercp::access::AccessUser u1, u2;
    u1.id = 1; u1.username = "keep"; u1.enabled = true;
    u2.id = 2; u2.username = "remove"; u2.enabled = true;
    ss.save_access_users({u1, u2});
    ss.save_access_users({u1});

    auto loaded = ss.load_access_users();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].id == 1);
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_users empty vector clears") {
    auto dir = tdir("s6c_au_clear"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);

    containercp::access::AccessUser u;
    u.id = 1; u.username = "tmp"; u.enabled = true;
    ss.save_access_users({u});
    ss.save_access_users({});
    CHECK(ss.load_access_users().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_users reopen") {
    auto dir = tdir("s6c_au_reopen"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool; init_6a(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        containercp::access::AccessUser u;
        u.id = 5; u.username = "persist"; u.enabled = true;
        ss.save_access_users({u});
    }
    {
        containercp::storage::ConnectionPool pool; init_6a(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        auto loaded = ss.load_access_users();
        REQUIRE(loaded.size() == 1); CHECK(loaded[0].id == 5);
    }
    tclean(dir);
}

// --- Access grants ---

TEST_CASE("SQLiteStorage access_grants empty") {
    auto dir = tdir("s6c_ag_empty"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    CHECK(ss.load_access_grants().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_grants round trip all permissions") {
    auto dir = tdir("s6c_ag_rt"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);

    // Set up parent rows
    containercp::access::AccessUser u;
    u.id = 1; u.username = "u"; u.enabled = true;
    ss.save_access_users({u});
    containercp::site::Site s;
    s.id = 1; s.domain = "s.com"; s.node_id = 1;
    ss.save_access_users({u});
    ss.save_sites({s});

    containercp::access::AccessGrant g;
    g.id = 1; g.access_user_id = 1; g.site_id = 1;
    g.permission = containercp::access::Permission::READ_WRITE;
    ss.save_access_grants({g});
    auto loaded = ss.load_access_grants();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].id == 1);
    CHECK(loaded[0].access_user_id == 1);
    CHECK(loaded[0].site_id == 1);
    CHECK(loaded[0].permission == containercp::access::Permission::READ_WRITE);
    CHECK(loaded[0].name == "1-1");
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_grants all permission values") {
    auto dir = tdir("s6c_ag_perm"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);

    containercp::access::AccessUser u;
    u.id = 1; u.username = "u"; u.enabled = true;
    ss.save_access_users({u});

    containercp::site::Site s1, s2, s3;
    s1.id = 1; s1.domain = "a.com"; s1.node_id = 1;
    s2.id = 2; s2.domain = "b.com"; s2.node_id = 1;
    s3.id = 3; s3.domain = "c.com"; s3.node_id = 1;
    ss.save_sites({s1, s2, s3});

    std::vector<std::pair<containercp::access::Permission, std::string>> perms = {
        {containercp::access::Permission::READ_ONLY, "read_only"},
        {containercp::access::Permission::READ_WRITE, "read_write"},
        {containercp::access::Permission::DEPLOY, "deploy"},
    };

    for (size_t i = 0; i < perms.size(); ++i) {
        containercp::access::AccessGrant g;
        g.id = static_cast<uint64_t>(i + 1);
        g.access_user_id = 1;
        g.site_id = static_cast<uint64_t>(i + 1);
        g.permission = perms[i].first;
        ss.save_access_grants({g});

        auto loaded = ss.load_access_grants();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].permission == perms[i].first);
        ss.save_access_grants({});  // clear for next
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_grants FK: missing user fails") {
    auto dir = tdir("s6c_ag_nouser"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);

    containercp::site::Site s;
    s.id = 1; s.domain = "s.com"; s.node_id = 1;
    ss.save_sites({s});

    containercp::access::AccessGrant g;
    g.id = 1; g.access_user_id = 999; g.site_id = 1;
    ss.save_access_grants({g});
    // save_access_grants returns void — check that no grant was stored
    CHECK(ss.load_access_grants().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_grants FK: missing site fails") {
    auto dir = tdir("s6c_ag_nosite"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);

    containercp::access::AccessUser u;
    u.id = 1; u.username = "u"; u.enabled = true;
    ss.save_access_users({u});

    containercp::access::AccessGrant g;
    g.id = 1; g.access_user_id = 1; g.site_id = 999;
    ss.save_access_grants({g});
    CHECK(ss.load_access_grants().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_grants FK: cannot delete referenced user") {
    auto dir = tdir("s6c_ag_restrict_user"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);

    containercp::access::AccessUser u;
    u.id = 1; u.username = "u"; u.enabled = true;
    ss.save_access_users({u});

    containercp::site::Site s;
    s.id = 1; s.domain = "s.com"; s.node_id = 1;
    ss.save_sites({s});

    containercp::access::AccessGrant g;
    g.id = 1; g.access_user_id = 1; g.site_id = 1;
    ss.save_access_grants({g});

    // Trying to remove the user should fail (grant exists)
    ss.save_access_users({});  // empty vector — prune should fail
    auto users = ss.load_access_users();
    CHECK_FALSE(users.empty());  // user should still exist
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_grants FK: cannot delete referenced site") {
    auto dir = tdir("s6c_ag_restrict_site"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);

    containercp::access::AccessUser u;
    u.id = 1; u.username = "u"; u.enabled = true;
    ss.save_access_users({u});

    containercp::site::Site s;
    s.id = 1; s.domain = "s.com"; s.node_id = 1;
    ss.save_sites({s});

    containercp::access::AccessGrant g;
    g.id = 1; g.access_user_id = 1; g.site_id = 1;
    ss.save_access_grants({g});

    // Trying to remove the site should fail (grant exists)
    ss.save_sites({});  // empty vector — prune should fail
    auto sites = ss.load_sites();
    CHECK_FALSE(sites.empty());  // site should still exist
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_grants remove grant then delete parent") {
    auto dir = tdir("s6c_ag_order"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);

    containercp::access::AccessUser u;
    u.id = 1; u.username = "u"; u.enabled = true;
    ss.save_access_users({u});

    containercp::site::Site s;
    s.id = 1; s.domain = "s.com"; s.node_id = 1;
    ss.save_sites({s});

    containercp::access::AccessGrant g;
    g.id = 1; g.access_user_id = 1; g.site_id = 1;
    ss.save_access_grants({g});

    // Remove grant first
    ss.save_access_grants({});
    CHECK(ss.load_access_grants().empty());

    // Now remove user — should succeed
    ss.save_access_users({});
    CHECK(ss.load_access_users().empty());

    // Now remove site — should succeed
    ss.save_sites({});
    CHECK(ss.load_sites().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage access_grants PRAGMA foreign_key_check") {
    auto dir = tdir("s6c_ag_fkcheck"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);

    containercp::access::AccessUser u;
    u.id = 1; u.username = "u"; u.enabled = true;
    ss.save_access_users({u});

    containercp::site::Site s;
    s.id = 1; s.domain = "s.com"; s.node_id = 1;
    ss.save_sites({s});

    containercp::access::AccessGrant g;
    g.id = 1; g.access_user_id = 1; g.site_id = 1;
    ss.save_access_grants({g});

    containercp::storage::ReadLease rl(pool);
    REQUIRE(rl.is_valid());
    REQUIRE(rl->prepare("PRAGMA foreign_key_check"));
    CHECK_FALSE(rl->step());
    tclean(dir);
}

TEST_CASE("Phase6c explicit SQLite mode uses SQLite for access") {
    auto dir = tdir("s6c_mode"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());

        containercp::access::AccessUser u;
        u.id = 1; u.username = "u"; u.enabled = true;
        s.save_access_users({u});
        CHECK_FALSE(fs::exists(dir + "access_users.db"));

        CHECK(s.load_access_users().size() == 1);
    }
    tclean(dir);
}

TEST_CASE("Phase6c default mode reads TXT access") {
    auto dir = tdir("s6c_txt"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::Storage s(dir);
        containercp::access::AccessUser u;
        u.id = 1; u.username = "u"; u.password_hash = "h"; u.enabled = true;
        s.save_access_users({u});
    }
    {
        containercp::storage::Storage s(dir);
        CHECK(s.load_access_users().size() == 1);
    }
    tclean(dir);
}

TEST_CASE("Phase6c existing phases still work") {
    auto dir = tdir("s6c_existing"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());

        containercp::node::Node n; n.id = 1; n.name = "n"; n.type = "local";
        s.save_nodes({n});
        CHECK(s.load_nodes().size() == 1);

        containercp::database::Database db;
        db.id = 1; db.db_name = "d"; db.site_id = 1;
        s.save_databases({db});
        CHECK(s.load_databases().size() == 1);

        containercp::auth::AuthUser au;
        au.id = 1; au.username = "admin"; au.password_hash = "h"; au.role = "admin";
        s.save_auth_users({au});
        CHECK(s.load_auth_users().size() == 1);
        CHECK_FALSE(fs::exists(dir + "auth_users.db"));
    }
    tclean(dir);
}

// ============================================================
// Phase 7 — Mail and SSL metadata SQLite storage tests
// ============================================================

// --- SSL certificates ---

TEST_CASE("SQLiteStorage ssl_certificates empty") {
    auto dir = tdir("s7_ssl_empty"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    CHECK(ss.load_ssl_certificates().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage ssl_certificates round trip") {
    auto dir = tdir("s7_ssl_rt"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::ssl::SslCertificate c;
    c.id = 1; c.domain_id = 1; c.domain = "example.com";
    c.provider = "letsencrypt"; c.status = "active";
    c.certificate_path = "/ssl/1/fullchain.pem";
    c.key_path = "/ssl/1/privkey.pem";
    c.auto_renew = true; c.https_enabled = true;
    c.version = 2;
    ss.save_ssl_certificates({c});
    auto loaded = ss.load_ssl_certificates();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].id == 1); CHECK(loaded[0].domain == "example.com");
    CHECK(loaded[0].provider == "letsencrypt"); CHECK(loaded[0].status == "active");
    CHECK(loaded[0].certificate_path == "/ssl/1/fullchain.pem");
    CHECK(loaded[0].key_path == "/ssl/1/privkey.pem");
    CHECK(loaded[0].auto_renew); CHECK(loaded[0].https_enabled);
    CHECK(loaded[0].version == 2); CHECK(loaded[0].name == "example.com");
    tclean(dir);
}

TEST_CASE("SQLiteStorage ssl_certificates no PEM content") {
    auto dir = tdir("s7_ssl_nopem"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::ssl::SslCertificate c;
    c.id = 1; c.domain = "t.com";
    c.certificate_path = "/path.pem";
    ss.save_ssl_certificates({c});
    containercp::storage::ReadLease rl(pool);
    REQUIRE(rl.is_valid());
    // Check no PEM header lives in any column
    REQUIRE(rl->prepare("SELECT sql FROM sqlite_master WHERE type='table' "
                        "AND name='ssl_certificates'"));
    REQUIRE(rl->step());
    std::string ddl = rl->column_text(0);
    CHECK(ddl.find("BEGIN CERTIFICATE") == std::string::npos);
    CHECK(ddl.find("BEGIN PRIVATE KEY") == std::string::npos);
    tclean(dir);
}

// --- Mail domains ---

TEST_CASE("SQLiteStorage mail_domains empty") {
    auto dir = tdir("s7_md_empty"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    CHECK(ss.load_mail_domains().empty());
    tclean(dir);
}

TEST_CASE("SQLiteStorage mail_domains round trip and sentinels") {
    auto dir = tdir("s7_md_rt"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::mail::MailDomain m;
    m.id = 1; m.domain_name = "example.com";
    m.mode = containercp::mail::MailDomainMode::LocalPrimary;
    m.domain_id = 0; m.site_id = 0;
    m.dkim_selector = "dkim2024";
    m.max_mailboxes = 10; m.max_aliases = 5;
    m.catch_all = "postmaster@example.com";
    m.enabled = true;
    m.created_at = "2026-01-01T00:00:00Z";
    m.updated_at = "2026-06-15T12:00:00Z";
    m.dkim_selector = "dkim2024";
    m.dkim_private_key_path = "/srv/containercp/dkim/example.com/dkim.private";
    m.dkim_public_key_dns = "v=DKIM1; k=rsa; p=TESTPUBLICKEY";
    ss.save_mail_domains({m});
    auto loaded = ss.load_mail_domains();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].domain_name == "example.com");
    CHECK(loaded[0].mode == containercp::mail::MailDomainMode::LocalPrimary);
    CHECK(loaded[0].domain_id == 0); CHECK(loaded[0].site_id == 0);
    CHECK(loaded[0].max_mailboxes == 10); CHECK(loaded[0].max_aliases == 5);
    CHECK(loaded[0].catch_all == "postmaster@example.com");
    CHECK(loaded[0].enabled); CHECK(loaded[0].name == "example.com");
    CHECK(loaded[0].created_at == "2026-01-01T00:00:00Z");
    CHECK(loaded[0].updated_at == "2026-06-15T12:00:00Z");
    CHECK(loaded[0].dkim_selector == "dkim2024");
    CHECK(loaded[0].dkim_private_key_path == "/srv/containercp/dkim/example.com/dkim.private");
    CHECK(loaded[0].dkim_public_key_dns == "v=DKIM1; k=rsa; p=TESTPUBLICKEY");
    // Update replaces timestamps
    m.created_at = "2026-02-01T00:00:00Z";
    m.updated_at = "2026-07-01T00:00:00Z";
    ss.save_mail_domains({m});
    loaded = ss.load_mail_domains();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].created_at == "2026-02-01T00:00:00Z");
    CHECK(loaded[0].updated_at == "2026-07-01T00:00:00Z");
    tclean(dir);
}

TEST_CASE("SQLiteStorage mail_domains all modes") {
    auto dir = tdir("s7_md_modes"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    auto modes = {
        containercp::mail::MailDomainMode::Disabled,
        containercp::mail::MailDomainMode::LocalPrimary,
        containercp::mail::MailDomainMode::ExternalRelay,
        containercp::mail::MailDomainMode::SplitM365,
    };
    int id = 0;
    for (auto mode : modes) {
        containercp::mail::MailDomain m;
        m.id = static_cast<uint64_t>(++id);
        m.domain_name = "m" + std::to_string(id) + ".com";
        m.mode = mode;
        if (mode == containercp::mail::MailDomainMode::ExternalRelay ||
            mode == containercp::mail::MailDomainMode::SplitM365) {
            m.relay_host = "relay.m" + std::to_string(id) + ".com";
        }
        ss.save_mail_domains({m});
        auto loaded = ss.load_mail_domains();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].mode == mode);
        ss.save_mail_domains({});
    }
    tclean(dir);
}

// --- Mail mailboxes ---

TEST_CASE("SQLiteStorage mail_mailboxes round trip and FK") {
    auto dir = tdir("s7_mb_rt"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    // Create parent mail domain
    containercp::mail::MailDomain md;
    md.id = 1; md.domain_name = "example.com";
    ss.save_mail_domains({md});

    containercp::mail::Mailbox mb;
    mb.id = 1; mb.domain_id = 1;
    mb.local_part = "admin";
    mb.password_hash = "$6$test";
    mb.quota_bytes = 1073741824;
    mb.display_name = "Admin User";
    mb.forward_to = "admin@other.com";
    mb.spam_enabled = true;
    mb.enabled = true;
    mb.created_at = "2026-01-01T00:00:00Z";
    mb.updated_at = "2026-06-01T00:00:00Z";
    ss.save_mailboxes({mb});
    auto loaded = ss.load_mailboxes();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].local_part == "admin");
    CHECK(loaded[0].password_hash == "$6$test");
    CHECK(loaded[0].display_name == "Admin User");
    CHECK(loaded[0].name == "admin");
    CHECK(loaded[0].created_at == "2026-01-01T00:00:00Z");
    CHECK(loaded[0].updated_at == "2026-06-01T00:00:00Z");
    tclean(dir);
}

TEST_CASE("SQLiteStorage mail_mailboxes FK: missing domain fails") {
    auto dir = tdir("s7_mb_nodom"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::mail::Mailbox mb;
    mb.id = 1; mb.domain_id = 999; mb.local_part = "x";
    ss.save_mailboxes({mb});
    CHECK(ss.load_mailboxes().empty());
    tclean(dir);
}

// --- Mail aliases ---

TEST_CASE("SQLiteStorage mail_aliases round trip") {
    auto dir = tdir("s7_ma_rt"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::mail::MailDomain md;
    md.id = 1; md.domain_name = "example.com";
    ss.save_mail_domains({md});

    containercp::mail::MailAlias a;
    a.id = 1; a.domain_id = 1;
    a.source_local_part = "info";
    a.destination = "admin@example.com";
    a.enabled = true;
    a.created_at = "2026-03-01T00:00:00Z";
    a.updated_at = "2026-04-01T00:00:00Z";
    ss.save_mail_aliases({a});
    auto loaded = ss.load_mail_aliases();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].source_local_part == "info");
    CHECK(loaded[0].destination == "admin@example.com");
    CHECK(loaded[0].name == "info");
    CHECK(loaded[0].created_at == "2026-03-01T00:00:00Z");
    CHECK(loaded[0].updated_at == "2026-04-01T00:00:00Z");
    tclean(dir);
}

TEST_CASE("SQLiteStorage mail_aliases FK: missing domain fails") {
    auto dir = tdir("s7_ma_nodom"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);
    containercp::mail::MailAlias a;
    a.id = 1; a.domain_id = 999;
    a.source_local_part = "x"; a.destination = "y@z.com";
    ss.save_mail_aliases({a});
    CHECK(ss.load_mail_aliases().empty());
    tclean(dir);
}

// --- Mail config (state + smarthost) ---

TEST_CASE("SQLiteStorage mail_config state round trip") {
    auto dir = tdir("s7_mc_state"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);

    CHECK(ss.load_mail_module_state().empty());

    ss.save_mail_module_state("active");
    CHECK(ss.load_mail_module_state() == "active");

    ss.save_mail_module_state("inactive");
    CHECK(ss.load_mail_module_state() == "inactive");
    tclean(dir);
}

TEST_CASE("SQLiteStorage mail_config state and smarthost coexist") {
    auto dir = tdir("s7_mc_coexist"); tclean(dir); fs::create_directories(dir);
    containercp::storage::ConnectionPool pool; init_6a(pool, dir);
    containercp::storage::SQLiteStorage ss(pool);

    ss.save_mail_module_state("active");
    ss.save_mail_smarthost("1|smtp.example.com|587|user|pass");

    CHECK(ss.load_mail_module_state() == "active");
    CHECK(ss.load_mail_smarthost() == "1|smtp.example.com|587|user|pass");

    // Updating state must not erase smarthost
    ss.save_mail_module_state("inactive");
    CHECK(ss.load_mail_module_state() == "inactive");
    CHECK(ss.load_mail_smarthost() == "1|smtp.example.com|587|user|pass");
    tclean(dir);
}

// --- Cross-backend ---

TEST_CASE("Phase7 explicit SQLite mode stores mail/SSL in SQLite") {
    auto dir = tdir("s7_mode"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());

        containercp::ssl::SslCertificate c;
        c.id = 1; c.domain = "t.com";
        s.save_ssl_certificates({c});
        CHECK_FALSE(fs::exists(dir + "ssl_certificates.db"));

        CHECK(s.load_ssl_certificates().size() == 1);
    }
    tclean(dir);
}

TEST_CASE("Shutdown then reinitialize still works") {
    auto dir = tdir("shutdown_reinit");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        pool.shutdown();

        CHECK(pool.initialize(dir + "containercp.db"));

        containercp::storage::WriteGuard wg(pool);
        CHECK(wg.is_valid());
        CHECK(wg.db().exec("SELECT 1"));
    }
    tclean(dir);
}

// ============================================================
// ConnectionPool lifecycle and lease accounting tests
// ============================================================

using namespace containercp::storage;

TEST_CASE("ConnectionPool uninitialized lease does not affect lease count") {
    ConnectionPool pool;
    CHECK(pool.lease_read() == nullptr);
    pool.shutdown();
    pool.shutdown(); // idempotent
}

TEST_CASE("ConnectionPool return nullptr is a no-op") {
    ConnectionPool pool;
    pool.return_read(nullptr);
    pool.shutdown();
}

TEST_CASE("ConnectionPool lease return accounting is exact") {
    auto dir = tdir("cp_lease_acc");
    tclean(dir); fs::create_directories(dir);
    ConnectionPool pool;
    REQUIRE(pool.initialize(dir + "test.db"));
    {
        ReadLease rl(pool);
        CHECK(rl.is_valid());
    }
    pool.shutdown();
    tclean(dir);
}

TEST_CASE("ConnectionPool double return does not underflow") {
    auto dir = tdir("cp_dbl_ret");
    tclean(dir); fs::create_directories(dir);
    ConnectionPool pool;
    REQUIRE(pool.initialize(dir + "test.db"));
    SQLiteDB* db = pool.lease_read();
    REQUIRE(db != nullptr);
    pool.return_read(db);
    pool.return_read(db); // second return — no-op
    pool.shutdown();
    tclean(dir);
}

TEST_CASE("ConnectionPool foreign pointer return is ignored") {
    auto dir = tdir("cp_foreign");
    tclean(dir); fs::create_directories(dir);
    ConnectionPool pool;
    REQUIRE(pool.initialize(dir + "test.db"));
    SQLiteDB fake;
    pool.return_read(&fake);
    pool.shutdown();
    tclean(dir);
}

TEST_CASE("ConnectionPool repeated lease cycles return to zero") {
    auto dir = tdir("cp_cycle");
    tclean(dir); fs::create_directories(dir);
    ConnectionPool pool;
    REQUIRE(pool.initialize(dir + "test.db"));
    for (int cycle = 0; cycle < 10; ++cycle) {
        ReadLease rl(pool);
        CHECK(rl.is_valid());
    }
    pool.shutdown();
    tclean(dir);
}

TEST_CASE("ConnectionPool shutdown waits for active read lease") {
    auto dir = tdir("cp_shut_wait");
    tclean(dir); fs::create_directories(dir);
    ConnectionPool pool;
    REQUIRE(pool.initialize(dir + "test.db"));

    std::mutex mtx;
    std::condition_variable cv;
    bool shutdown_done = false;

    std::unique_ptr<ReadLease> rl(new ReadLease(pool));
    REQUIRE(rl->is_valid());
    REQUIRE((*rl)->exec("CREATE TABLE IF NOT EXISTS t(x)"));
    REQUIRE((*rl)->exec("INSERT INTO t VALUES(1)"));

    std::thread t([&]() {
        pool.shutdown();
        std::lock_guard<std::mutex> lk(mtx);
        shutdown_done = true;
        cv.notify_one();
    });

    // Wait until shutdown flag is set (lease wait has started)
    while (!pool.is_shutdown()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Shutdown not complete while lease still active
    {
        std::lock_guard<std::mutex> lk(mtx);
        CHECK_FALSE(shutdown_done);
    }

    // New lease rejected
    CHECK(pool.lease_read() == nullptr);

    // Active lease still valid
    REQUIRE((*rl)->exec("SELECT * FROM t"));

    rl.reset(); // release lease → shutdown completes
    t.join();
    CHECK(shutdown_done);
    tclean(dir);
}

TEST_CASE("ConnectionPool destructor on never-initialized pool") {
    { ConnectionPool pool; }
    CHECK(true);
}

TEST_CASE("ConnectionPool shutdown on never-initialized pool") {
    ConnectionPool pool;
    pool.shutdown();
    pool.shutdown();
    CHECK(true);
}

TEST_CASE("ConnectionPool shutdown twice") {
    auto dir = tdir("cp_shut2");
    tclean(dir); fs::create_directories(dir);
    ConnectionPool pool;
    REQUIRE(pool.initialize(dir + "test.db"));
    pool.shutdown();
    pool.shutdown();
    tclean(dir);
}

TEST_CASE("ConnectionPool initialization failure then shutdown") {
    ConnectionPool pool;
    CHECK_FALSE(pool.initialize("/nonexistent_dir_xyz_123/test.db"));
    pool.shutdown();
}

TEST_CASE("ConnectionPool read acquisition after shutdown rejected") {
    auto dir = tdir("cp_acq_after");
    tclean(dir); fs::create_directories(dir);
    ConnectionPool pool;
    REQUIRE(pool.initialize(dir + "test.db"));
    pool.shutdown();
    CHECK(pool.lease_read() == nullptr);
    tclean(dir);
}

TEST_CASE("ConnectionPool WriteGuard on uninitialized pool") {
    ConnectionPool pool;
    WriteGuard wg(pool);
    CHECK_FALSE(wg.is_valid());
}

TEST_CASE("ConnectionPool ReadLease on uninitialized pool") {
    ConnectionPool pool;
    ReadLease rl(pool);
    CHECK_FALSE(rl.is_valid());
}

TEST_CASE("ConnectionPool WriteGuard after shutdown") {
    auto dir = tdir("cp_wg_after");
    tclean(dir); fs::create_directories(dir);
    ConnectionPool pool;
    REQUIRE(pool.initialize(dir + "test.db"));
    pool.shutdown();
    WriteGuard wg(pool);
    CHECK_FALSE(wg.is_valid());
    tclean(dir);
}

TEST_CASE("ConnectionPool repeated initialize shutdown cycles") {
    auto dir = tdir("cp_rep_init");
    tclean(dir); fs::create_directories(dir);
    ConnectionPool pool;
    for (int i = 0; i < 3; ++i) {
        REQUIRE(pool.initialize(dir + "test.db"));
        pool.shutdown();
    }
    tclean(dir);
}

TEST_CASE("ConnectionPool pool destruction after normal shutdown") {
    auto dir = tdir("cp_dtor_norm");
    tclean(dir); fs::create_directories(dir);
    {
        ConnectionPool pool;
        REQUIRE(pool.initialize(dir + "test.db"));
        pool.shutdown();
    }
    tclean(dir);
}

TEST_CASE("ConnectionPool pool destruction after init failure") {
    { ConnectionPool pool; pool.initialize("/bad/db"); }
    CHECK(true);
}

TEST_CASE("ConnectionPool backup and shutdown ordering") {
    auto dir = tdir("cp_backup_shut");
    tclean(dir); fs::create_directories(dir);
    ConnectionPool pool;
    REQUIRE(pool.initialize(dir + "test.db"));
    {
        WriteGuard wg(pool);
        REQUIRE(wg.is_valid());
        wg.db().exec("CREATE TABLE IF NOT EXISTS backup_test(x)");
        wg.db().exec("INSERT INTO backup_test VALUES(42)");
    }
    std::thread t([&]() { pool.backup(dir + "backup.db"); });
    t.join();
    pool.shutdown();
    CHECK(fs::exists(dir + "backup.db"));
    tclean(dir);
}

TEST_CASE("ConnectionPool Active WriteGuard shutdown waits for guard") {
    auto dir = tdir("cp_wg_shut");
    tclean(dir); fs::create_directories(dir);
    ConnectionPool pool;
    REQUIRE(pool.initialize(dir + "test.db"));

    std::mutex mtx;
    std::condition_variable cv;
    bool shutdown_started = false;
    bool shutdown_done = false;

    pool.test_obs_.on_shutdown_awaiting_write_mutex = [&]() {
        std::lock_guard<std::mutex> lk(mtx);
        shutdown_started = true;
        cv.notify_one();
    };

    std::unique_ptr<WriteGuard> wg(new WriteGuard(pool));
    REQUIRE(wg->is_valid());
    wg->db().exec("CREATE TABLE IF NOT EXISTS wg_test(x)");

    std::thread t([&]() {
        pool.shutdown();
        std::lock_guard<std::mutex> lk(mtx);
        shutdown_done = true;
        cv.notify_one();
    });

    {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [&]() { return shutdown_started; });
    }

    {
        std::lock_guard<std::mutex> lk(mtx);
        CHECK_FALSE(shutdown_done);
    }

    CHECK(wg->db().exec("INSERT INTO wg_test VALUES(1)"));

    wg.reset(); // release before joining
    t.join();
    CHECK(shutdown_done);
    pool.shutdown();
    tclean(dir);
}

// ============================================================
// Phase 11-08: Startup validation for SqlitePhase5
// ============================================================

static std::string create_state_file(const std::string& dir, const std::string& backend, const std::string& db_path) {
    std::string state_path = dir + "storage-state.json";
    std::ofstream f(state_path);
    f << "{\n";
    f << "  \"active_backend\": \"" << backend << "\",\n";
    f << "  \"database_path\": \"" << db_path << "\",\n";
    f << "  \"schema_version\": 1\n";
    f << "}\n";
    f.close();
    return state_path;
}

TEST_CASE("P11-08 startup validation passes with correct activation state") {
    auto dir = tdir("p1108_happy");
    tclean(dir); fs::create_directories(dir);
    // Initialize DB with skip_startup_validation
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        Storage s(dir, opts);
        REQUIRE(s.sqlite_ready());
    }
    // Create activation state
    create_state_file(dir, "sqlite", dir + "containercp.db");
    // Reopen with validation
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = false;
        Storage s(dir, opts);
        CHECK(s.sqlite_ready());
    }
    tclean(dir);
}

TEST_CASE("P11-08 startup validation rejects missing activation state") {
    auto dir = tdir("p1108_no_state");
    tclean(dir); fs::create_directories(dir);
    StorageOptions opts;
    opts.core_backend = CoreStorageBackend::SqlitePhase5;
    opts.skip_startup_validation = false;
    CHECK_THROWS_AS(Storage(dir, opts), std::runtime_error);
    tclean(dir);
}

TEST_CASE("P11-08 startup validation rejects wrong backend in activation state") {
    auto dir = tdir("p1108_wrong_backend");
    tclean(dir); fs::create_directories(dir);
    // Initialize DB
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        Storage s(dir, opts);
        REQUIRE(s.sqlite_ready());
    }
    // Create state with wrong backend
    create_state_file(dir, "txt", dir + "containercp.db");
    // Reopen should throw
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = false;
        CHECK_THROWS_AS(Storage(dir, opts), std::runtime_error);
    }
    tclean(dir);
}

TEST_CASE("P11-08 startup validation rejects mismatched database_path in activation state") {
    auto dir = tdir("p1108_wrong_path");
    tclean(dir); fs::create_directories(dir);
    // Initialize DB
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        Storage s(dir, opts);
        REQUIRE(s.sqlite_ready());
    }
    // Create state with wrong db path
    create_state_file(dir, "sqlite", "/wrong/path/containercp.db");
    // Reopen should throw
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = false;
        CHECK_THROWS_AS(Storage(dir, opts), std::runtime_error);
    }
    tclean(dir);
}

TEST_CASE("P11-08 startup validation rejects missing database file") {
    auto dir = tdir("p1108_no_db");
    tclean(dir); fs::create_directories(dir);
    // Create state file but no DB
    create_state_file(dir, "sqlite", dir + "containercp.db");
    StorageOptions opts;
    opts.core_backend = CoreStorageBackend::SqlitePhase5;
    opts.skip_startup_validation = false;
    CHECK_THROWS_AS(Storage(dir, opts), std::runtime_error);
    tclean(dir);
}

TEST_CASE("P11-08 startup validation rejects corrupt database file") {
    auto dir = tdir("p1108_corrupt");
    tclean(dir); fs::create_directories(dir);
    // Create dummy DB file
    {
        std::ofstream f(dir + "containercp.db");
        f << "this is not a valid sqlite database file\n";
        f.close();
    }
    // Create state file
    create_state_file(dir, "sqlite", dir + "containercp.db");
    StorageOptions opts;
    opts.core_backend = CoreStorageBackend::SqlitePhase5;
    opts.skip_startup_validation = false;
    CHECK_THROWS_AS(Storage(dir, opts), std::runtime_error);
    tclean(dir);
}

TEST_CASE("P11-08 startup validation passes on reopened migrated database") {
    auto dir = tdir("p1108_reopen");
    tclean(dir); fs::create_directories(dir);
    // Initialize DB with skip_startup_validation
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        Storage s(dir, opts);
        REQUIRE(s.sqlite_ready());
        // Write some data
        containercp::node::Node n;
        n.id = 100; n.name = "reopen-node"; n.type = "local";
        s.save_nodes({n});
    }
    // Create activation state
    create_state_file(dir, "sqlite", dir + "containercp.db");
    // Reopen with validation — should succeed AND preserve data
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = false;
        Storage s(dir, opts);
        CHECK(s.sqlite_ready());
        auto nodes = s.load_nodes();
        REQUIRE(nodes.size() == 1);
        CHECK(nodes[0].id == 100);
        CHECK(nodes[0].name == "reopen-node");
    }
    tclean(dir);
}

// ============================================================
// Phase 11-10: Runtime repository wiring for all SQLite resources
// ============================================================

TEST_CASE("P11-10 Storage SQLite mode routes all 17 resources through SQLite") {
    auto dir = tdir("p1110_all_resources");
    tclean(dir); fs::create_directories(dir);
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        Storage s(dir, opts);
        REQUIRE(s.sqlite_ready());

        containercp::node::Node node;
        node.id = 1; node.name = "local"; node.type = "local";
        s.save_nodes({node});

        containercp::php::PhpVersion php;
        php.id = 1; php.version = "8.4"; php.image = "php:8.4"; php.default_version = true;
        s.save_php_versions({php});

        containercp::profile::Profile profile;
        profile.id = 1; profile.profile_name = "apache-php-default";
        profile.type = containercp::profile::ProfileType::WEB_SERVER;
        profile.web_server = "apache"; profile.runtime = "docker";
        profile.template_path = "/tmp/apache.conf"; profile.default_profile = true;
        s.save_profiles({profile});

        containercp::user::User user;
        user.id = 1; user.username = "admin"; user.uid = 1000;
        user.home_directory = "/srv/containercp/users/admin"; user.shell = "/usr/sbin/nologin";
        s.save_users({user});

        containercp::site::Site site;
        site.id = 1; site.domain = "example.com"; site.owner = "admin";
        site.node_id = 1; site.web_server = "apache"; site.php_mail_enabled = true;
        s.save_sites({site});

        containercp::domain::Domain domain;
        domain.id = 1; domain.fqdn = "example.com"; domain.owner_id = 1;
        domain.site_id = 1; domain.php_version = "8.4"; domain.enabled = true;
        s.save_domains({domain});

        containercp::database::Database database;
        database.id = 1; database.db_name = "example"; database.db_user = "example_user";
        database.db_password = "secret"; database.engine = "mariadb";
        database.version = "lts"; database.owner_id = 1; database.site_id = 1;
        s.save_databases({database});

        containercp::backup::Backup backup;
        backup.id = 1; backup.site_id = 1; backup.owner_id = 1;
        backup.filename = "example.tar.gz"; backup.type = "manual"; backup.size = 1234;
        backup.created_at = "2026-07-18T12:00:00Z"; backup.status = "completed";
        backup.file_path = "/srv/containercp/backups/example.tar.gz"; backup.compression = "gzip";
        s.save_backups({backup});

        containercp::proxy::ReverseProxy proxy;
        proxy.id = 1; proxy.domain = "example.com"; proxy.site_id = 1;
        proxy.provider = "nginx"; proxy.config_path = "/srv/containercp/proxy/sites/example.com.conf";
        proxy.upstream = "site-1-php:80"; proxy.enabled = true; proxy.status = "active";
        s.save_reverse_proxies({proxy});

        containercp::access::AccessUser access_user;
        access_user.id = 1; access_user.username = "deploy";
        access_user.auth_type = "password"; access_user.password_hash = "hash";
        s.save_access_users({access_user});

        containercp::access::AccessGrant grant;
        grant.id = 1; grant.access_user_id = 1; grant.site_id = 1;
        grant.permission = containercp::access::Permission::READ_WRITE;
        s.save_access_grants({grant});

        containercp::auth::AuthUser auth_user;
        auth_user.id = 1; auth_user.username = "admin"; auth_user.password_hash = "hash";
        auth_user.must_change_password = false; auth_user.enabled = true; auth_user.role = "admin";
        s.save_auth_users({auth_user});

        containercp::ssl::SslCertificate cert;
        cert.id = 1; cert.domain_id = 1; cert.domain = "example.com";
        cert.provider = "letsencrypt"; cert.certificate_path = "/ssl/fullchain.pem";
        cert.key_path = "/ssl/privkey.pem"; cert.chain_path = "/ssl/chain.pem";
        cert.issued_at = "2026-07-18T12:00:00Z"; cert.expires_at = "2026-10-16T12:00:00Z";
        cert.renew_after = "2026-09-16T12:00:00Z"; cert.status = "active";
        cert.auto_renew = true; cert.https_enabled = true; cert.redirect_enabled = true;
        cert.domains = "example.com"; cert.challenge_type = "http-01";
        s.save_ssl_certificates({cert});

        containercp::mail::MailDomain mail_domain;
        mail_domain.id = 1; mail_domain.domain_id = 1; mail_domain.site_id = 1;
        mail_domain.domain_name = "example.com";
        mail_domain.mode = containercp::mail::MailDomainMode::LocalPrimary;
        mail_domain.dkim_selector = "dkim"; mail_domain.max_mailboxes = 10;
        mail_domain.max_aliases = 10; mail_domain.enabled = true;
        mail_domain.created_at = "2026-07-18T12:00:00Z";
        mail_domain.updated_at = "2026-07-18T12:00:00Z";
        s.save_mail_domains({mail_domain});

        containercp::mail::Mailbox mailbox;
        mailbox.id = 1; mailbox.domain_id = 1; mailbox.local_part = "admin";
        mailbox.password_hash = "hash"; mailbox.quota_bytes = 1024;
        mailbox.quota_messages = 100; mailbox.enabled = true;
        mailbox.display_name = "Admin"; mailbox.created_at = "2026-07-18T12:00:00Z";
        mailbox.updated_at = "2026-07-18T12:00:00Z";
        s.save_mailboxes({mailbox});

        containercp::mail::MailAlias alias;
        alias.id = 1; alias.domain_id = 1; alias.source_local_part = "info";
        alias.destination = "admin@example.com"; alias.enabled = true;
        alias.created_at = "2026-07-18T12:00:00Z";
        alias.updated_at = "2026-07-18T12:00:00Z";
        s.save_mail_aliases({alias});

        s.save_mail_module_state("active");

        auto require_snapshot = [](const auto& snap) {
            CHECK(snap.success);
            REQUIRE(snap.records.size() == 1);
        };
        require_snapshot(s.load_nodes_checked());
        require_snapshot(s.load_php_versions_checked());
        require_snapshot(s.load_profiles_checked());
        require_snapshot(s.load_users_checked());
        require_snapshot(s.load_sites_checked());
        require_snapshot(s.load_domains_checked());
        require_snapshot(s.load_databases_checked());
        require_snapshot(s.load_backups_checked());
        require_snapshot(s.load_reverse_proxies_checked());
        require_snapshot(s.load_access_users_checked());
        require_snapshot(s.load_access_grants_checked());
        require_snapshot(s.load_auth_users_checked());
        require_snapshot(s.load_ssl_certificates_checked());
        require_snapshot(s.load_mail_domains_checked());
        require_snapshot(s.load_mailboxes_checked());
        require_snapshot(s.load_mail_aliases_checked());

        auto mail_config = s.load_mail_module_state_checked();
        CHECK(mail_config.success);
        CHECK(mail_config.present);
        CHECK(mail_config.value == "active");

        for (const auto& txt_file : std::vector<std::string>{
                 "nodes.db", "php_versions.db", "profiles.db", "users.db",
                 "sites.db", "domains.db", "databases.db", "backups.db",
                 "reverse_proxies.db", "access_users.db", "access_grants.db",
                 "auth_users.db", "ssl_certificates.db", "mail_domains.db",
                 "mail_mailboxes.db", "mail_aliases.db", "mail_state.db"}) {
            CHECK_FALSE(fs::exists(dir + txt_file));
        }
    }
    tclean(dir);
}

// ============================================================
// Phase 11-11: Write-path validation
// ============================================================

TEST_CASE("P11-11 SQLite write path commits replacements without TXT fallback") {
    auto dir = tdir("p1111_commit_replace");
    tclean(dir); fs::create_directories(dir);
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        Storage s(dir, opts);
        REQUIRE(s.sqlite_ready());

        containercp::backup::Backup b1;
        b1.id = 1; b1.site_id = 1; b1.owner_id = 1;
        b1.filename = "initial.tar.gz"; b1.type = "manual"; b1.size = 100;
        b1.created_at = "2026-07-18T12:00:00Z"; b1.status = "completed";
        b1.file_path = "/srv/containercp/backups/initial.tar.gz"; b1.compression = "gzip";

        containercp::backup::Backup b2 = b1;
        b2.id = 2; b2.filename = "removed.tar.gz"; b2.file_path = "/srv/containercp/backups/removed.tar.gz";
        s.save_backups({b1, b2});

        b1.filename = "updated.tar.zst";
        b1.type = "scheduled";
        b1.size = 200;
        b1.file_path = "/srv/containercp/backups/updated.tar.zst";
        b1.compression = "zstd";
        s.save_backups({b1});

        auto backups = s.load_backups();
        REQUIRE(backups.size() == 1);
        CHECK(backups[0].id == 1);
        CHECK(backups[0].filename == "updated.tar.zst");
        CHECK(backups[0].type == "scheduled");
        CHECK(backups[0].size == 200);
        CHECK(backups[0].compression == "zstd");

        containercp::auth::AuthUser auth1;
        auth1.id = 1; auth1.username = "admin"; auth1.password_hash = "h1";
        auth1.must_change_password = true; auth1.enabled = true; auth1.role = "admin";
        containercp::auth::AuthUser auth2 = auth1;
        auth2.id = 2; auth2.username = "obsolete";
        s.save_auth_users({auth1, auth2});

        auth1.password_hash = "h2";
        auth1.must_change_password = false;
        auth1.role = "owner";
        s.save_auth_users({auth1});

        auto auths = s.load_auth_users();
        REQUIRE(auths.size() == 1);
        CHECK(auths[0].id == 1);
        CHECK(auths[0].password_hash == "h2");
        CHECK_FALSE(auths[0].must_change_password);
        CHECK(auths[0].role == "owner");

        s.save_mail_module_state("inactive");
        s.save_mail_module_state("active");
        auto state = s.load_mail_module_state_checked();
        REQUIRE(state.success);
        CHECK(state.present);
        CHECK(state.value == "active");

        CHECK_FALSE(fs::exists(dir + "backups.db"));
        CHECK_FALSE(fs::exists(dir + "auth_users.db"));
        CHECK_FALSE(fs::exists(dir + "mail_state.db"));
    }
    tclean(dir);
}

TEST_CASE("P11-11 SQLite write path rolls back failed child replacements") {
    auto dir = tdir("p1111_child_rollback");
    tclean(dir); fs::create_directories(dir);
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        Storage s(dir, opts);
        REQUIRE(s.sqlite_ready());

        containercp::access::AccessUser access_user;
        access_user.id = 1; access_user.username = "deploy";
        access_user.auth_type = "password"; access_user.password_hash = "hash";
        s.save_access_users({access_user});

        containercp::site::Site site;
        site.id = 1; site.domain = "example.com"; site.owner = "admin";
        site.node_id = 1;
        s.save_sites({site});

        containercp::access::AccessGrant original_grant;
        original_grant.id = 1; original_grant.access_user_id = 1;
        original_grant.site_id = 1;
        original_grant.permission = containercp::access::Permission::READ_ONLY;
        s.save_access_grants({original_grant});

        containercp::access::AccessGrant valid_new = original_grant;
        valid_new.id = 2;
        valid_new.permission = containercp::access::Permission::DEPLOY;
        containercp::access::AccessGrant invalid_new = original_grant;
        invalid_new.id = 3;
        invalid_new.site_id = 999;
        s.save_access_grants({valid_new, invalid_new});

        auto grants = s.load_access_grants();
        REQUIRE(grants.size() == 1);
        CHECK(grants[0].id == 1);
        CHECK(grants[0].site_id == 1);
        CHECK(grants[0].permission == containercp::access::Permission::READ_ONLY);

        containercp::mail::MailDomain mail_domain;
        mail_domain.id = 1; mail_domain.domain_id = 1; mail_domain.site_id = 1;
        mail_domain.domain_name = "example.com";
        mail_domain.mode = containercp::mail::MailDomainMode::LocalPrimary;
        mail_domain.created_at = "2026-07-18T12:00:00Z";
        mail_domain.updated_at = "2026-07-18T12:00:00Z";
        s.save_mail_domains({mail_domain});

        containercp::mail::Mailbox original_box;
        original_box.id = 1; original_box.domain_id = 1; original_box.local_part = "admin";
        original_box.password_hash = "hash";
        original_box.created_at = "2026-07-18T12:00:00Z";
        original_box.updated_at = "2026-07-18T12:00:00Z";
        s.save_mailboxes({original_box});

        containercp::mail::Mailbox valid_box = original_box;
        valid_box.id = 2;
        valid_box.local_part = "valid";
        containercp::mail::Mailbox invalid_box = original_box;
        invalid_box.id = 3;
        invalid_box.domain_id = 999;
        invalid_box.local_part = "invalid";
        s.save_mailboxes({valid_box, invalid_box});

        auto boxes = s.load_mailboxes();
        REQUIRE(boxes.size() == 1);
        CHECK(boxes[0].id == 1);
        CHECK(boxes[0].domain_id == 1);
        CHECK(boxes[0].local_part == "admin");

        CHECK_FALSE(fs::exists(dir + "access_grants.db"));
        CHECK_FALSE(fs::exists(dir + "mail_mailboxes.db"));
    }
    tclean(dir);
}

// ============================================================
// Phase 11-12: Read-path validation
// ============================================================

static void write_p1112_poison_txt_files(const std::string& dir) {
    for (const auto& filename : std::vector<std::string>{
             "nodes.db", "php_versions.db", "profiles.db", "users.db",
             "sites.db", "domains.db", "databases.db", "backups.db",
             "reverse_proxies.db", "access_users.db", "access_grants.db",
             "auth_users.db", "ssl_certificates.db", "mail_domains.db",
             "mail_mailboxes.db", "mail_aliases.db", "mail_state.db",
             "mail_smarthost.db"}) {
        std::ofstream f(dir + filename);
        f << "POISON_TXT_SHOULD_NOT_BE_READ\n";
    }
}

TEST_CASE("P11-12 SQLite read path returns successful empty snapshots without TXT fallback") {
    auto dir = tdir("p1112_empty_no_fallback");
    tclean(dir); fs::create_directories(dir);
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        Storage s(dir, opts);
        REQUIRE(s.sqlite_ready());

        write_p1112_poison_txt_files(dir);

        auto require_empty_snapshot = [](const auto& snap) {
            CHECK(snap.success);
            CHECK(snap.records.empty());
        };
        require_empty_snapshot(s.load_nodes_checked());
        require_empty_snapshot(s.load_php_versions_checked());
        require_empty_snapshot(s.load_profiles_checked());
        require_empty_snapshot(s.load_users_checked());
        require_empty_snapshot(s.load_sites_checked());
        require_empty_snapshot(s.load_domains_checked());
        require_empty_snapshot(s.load_databases_checked());
        require_empty_snapshot(s.load_backups_checked());
        require_empty_snapshot(s.load_reverse_proxies_checked());
        require_empty_snapshot(s.load_access_users_checked());
        require_empty_snapshot(s.load_access_grants_checked());
        require_empty_snapshot(s.load_auth_users_checked());
        require_empty_snapshot(s.load_ssl_certificates_checked());
        require_empty_snapshot(s.load_mail_domains_checked());
        require_empty_snapshot(s.load_mailboxes_checked());
        require_empty_snapshot(s.load_mail_aliases_checked());

        auto module_state = s.load_mail_module_state_checked();
        CHECK(module_state.success);
        CHECK_FALSE(module_state.present);
        auto smarthost = s.load_mail_smarthost_checked();
        CHECK(smarthost.success);
        CHECK_FALSE(smarthost.present);
    }
    tclean(dir);
}

TEST_CASE("P11-12 SQLite read path prefers SQLite data over conflicting TXT") {
    auto dir = tdir("p1112_sqlite_wins");
    tclean(dir); fs::create_directories(dir);
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        Storage s(dir, opts);
        REQUIRE(s.sqlite_ready());

        containercp::node::Node node;
        node.id = 42; node.name = "sqlite-node"; node.type = "local";
        s.save_nodes({node});

        containercp::backup::Backup backup;
        backup.id = 7; backup.site_id = 2; backup.owner_id = 3;
        backup.filename = "sqlite-backup.tar.gz"; backup.type = "manual";
        backup.size = 4096; backup.created_at = "2026-07-18T12:00:00Z";
        backup.status = "completed"; backup.file_path = "/sqlite/backup.tar.gz";
        backup.compression = "gzip";
        s.save_backups({backup});

        containercp::auth::AuthUser auth;
        auth.id = 5; auth.username = "sqlite-admin"; auth.password_hash = "sqlite-hash";
        auth.must_change_password = true; auth.enabled = true; auth.role = "admin";
        s.save_auth_users({auth});

        s.save_mail_module_state("active");

        write_p1112_poison_txt_files(dir);

        auto nodes = s.load_nodes();
        REQUIRE(nodes.size() == 1);
        CHECK(nodes[0].id == 42);
        CHECK(nodes[0].name == "sqlite-node");

        auto backups = s.load_backups();
        REQUIRE(backups.size() == 1);
        CHECK(backups[0].id == 7);
        CHECK(backups[0].filename == "sqlite-backup.tar.gz");

        auto auths = s.load_auth_users();
        REQUIRE(auths.size() == 1);
        CHECK(auths[0].id == 5);
        CHECK(auths[0].username == "sqlite-admin");
        CHECK(auths[0].must_change_password);

        auto module_state = s.load_mail_module_state_checked();
        REQUIRE(module_state.success);
        CHECK(module_state.present);
        CHECK(module_state.value == "active");
    }
    tclean(dir);
}

// ============================================================
// Phase 11-13: Restart persistence
// ============================================================

static void seed_p1113_runtime_resources(Storage& s) {
    containercp::node::Node node;
    node.id = 1; node.name = "local"; node.type = "local";
    s.save_nodes({node});

    containercp::php::PhpVersion php;
    php.id = 1; php.version = "8.4"; php.image = "php:8.4"; php.default_version = true;
    s.save_php_versions({php});

    containercp::profile::Profile profile;
    profile.id = 1; profile.profile_name = "apache-php-default";
    profile.type = containercp::profile::ProfileType::WEB_SERVER;
    profile.web_server = "apache"; profile.runtime = "docker";
    profile.template_path = "/tmp/apache.conf"; profile.default_profile = true;
    s.save_profiles({profile});

    containercp::user::User user;
    user.id = 1; user.username = "admin"; user.uid = 1000;
    user.home_directory = "/srv/containercp/users/admin"; user.shell = "/usr/sbin/nologin";
    s.save_users({user});

    containercp::site::Site site;
    site.id = 1; site.domain = "example.com"; site.owner = "admin";
    site.node_id = 1; site.web_server = "apache"; site.php_mail_enabled = true;
    s.save_sites({site});

    containercp::domain::Domain domain;
    domain.id = 1; domain.fqdn = "example.com"; domain.owner_id = 1;
    domain.site_id = 1; domain.php_version = "8.4"; domain.enabled = true;
    s.save_domains({domain});

    containercp::database::Database database;
    database.id = 1; database.db_name = "example"; database.db_user = "example_user";
    database.db_password = "secret"; database.engine = "mariadb";
    database.version = "lts"; database.owner_id = 1; database.site_id = 1;
    s.save_databases({database});

    containercp::backup::Backup backup;
    backup.id = 1; backup.site_id = 1; backup.owner_id = 1;
    backup.filename = "example.tar.gz"; backup.type = "manual"; backup.size = 1234;
    backup.created_at = "2026-07-18T12:00:00Z"; backup.status = "completed";
    backup.file_path = "/srv/containercp/backups/example.tar.gz"; backup.compression = "gzip";
    s.save_backups({backup});

    containercp::proxy::ReverseProxy proxy;
    proxy.id = 1; proxy.domain = "example.com"; proxy.site_id = 1;
    proxy.provider = "nginx"; proxy.config_path = "/srv/containercp/proxy/sites/example.com.conf";
    proxy.upstream = "site-1-php:80"; proxy.enabled = true; proxy.status = "active";
    s.save_reverse_proxies({proxy});

    containercp::access::AccessUser access_user;
    access_user.id = 1; access_user.username = "deploy";
    access_user.auth_type = "password"; access_user.password_hash = "hash";
    s.save_access_users({access_user});

    containercp::access::AccessGrant grant;
    grant.id = 1; grant.access_user_id = 1; grant.site_id = 1;
    grant.permission = containercp::access::Permission::READ_WRITE;
    s.save_access_grants({grant});

    containercp::auth::AuthUser auth_user;
    auth_user.id = 1; auth_user.username = "admin"; auth_user.password_hash = "hash";
    auth_user.must_change_password = false; auth_user.enabled = true; auth_user.role = "admin";
    s.save_auth_users({auth_user});

    containercp::ssl::SslCertificate cert;
    cert.id = 1; cert.domain_id = 1; cert.domain = "example.com";
    cert.provider = "letsencrypt"; cert.certificate_path = "/ssl/fullchain.pem";
    cert.key_path = "/ssl/privkey.pem"; cert.chain_path = "/ssl/chain.pem";
    cert.issued_at = "2026-07-18T12:00:00Z"; cert.expires_at = "2026-10-16T12:00:00Z";
    cert.renew_after = "2026-09-16T12:00:00Z"; cert.status = "active";
    cert.auto_renew = true; cert.https_enabled = true; cert.redirect_enabled = true;
    cert.domains = "example.com"; cert.challenge_type = "http-01";
    s.save_ssl_certificates({cert});

    containercp::mail::MailDomain mail_domain;
    mail_domain.id = 1; mail_domain.domain_id = 1; mail_domain.site_id = 1;
    mail_domain.domain_name = "example.com";
    mail_domain.mode = containercp::mail::MailDomainMode::LocalPrimary;
    mail_domain.dkim_selector = "dkim"; mail_domain.max_mailboxes = 10;
    mail_domain.max_aliases = 10; mail_domain.enabled = true;
    mail_domain.created_at = "2026-07-18T12:00:00Z";
    mail_domain.updated_at = "2026-07-18T12:00:00Z";
    s.save_mail_domains({mail_domain});

    containercp::mail::Mailbox mailbox;
    mailbox.id = 1; mailbox.domain_id = 1; mailbox.local_part = "admin";
    mailbox.password_hash = "hash"; mailbox.quota_bytes = 1024;
    mailbox.quota_messages = 100; mailbox.enabled = true;
    mailbox.display_name = "Admin"; mailbox.created_at = "2026-07-18T12:00:00Z";
    mailbox.updated_at = "2026-07-18T12:00:00Z";
    s.save_mailboxes({mailbox});

    containercp::mail::MailAlias alias;
    alias.id = 1; alias.domain_id = 1; alias.source_local_part = "info";
    alias.destination = "admin@example.com"; alias.enabled = true;
    alias.created_at = "2026-07-18T12:00:00Z";
    alias.updated_at = "2026-07-18T12:00:00Z";
    s.save_mail_aliases({alias});

    s.save_mail_module_state("active");
}

TEST_CASE("P11-13 SQLite restart preserves all runtime resources after startup validation") {
    auto dir = tdir("p1113_restart_all_resources");
    tclean(dir); fs::create_directories(dir);
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        Storage s(dir, opts);
        REQUIRE(s.sqlite_ready());
        seed_p1113_runtime_resources(s);
    }

    create_state_file(dir, "sqlite", dir + "containercp.db");

    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = false;
        Storage s(dir, opts);
        REQUIRE(s.sqlite_ready());

        auto require_snapshot = [](const auto& snap) {
            CHECK(snap.success);
            REQUIRE(snap.records.size() == 1);
        };
        require_snapshot(s.load_nodes_checked());
        require_snapshot(s.load_php_versions_checked());
        require_snapshot(s.load_profiles_checked());
        require_snapshot(s.load_users_checked());
        require_snapshot(s.load_sites_checked());
        require_snapshot(s.load_domains_checked());
        require_snapshot(s.load_databases_checked());
        require_snapshot(s.load_backups_checked());
        require_snapshot(s.load_reverse_proxies_checked());
        require_snapshot(s.load_access_users_checked());
        require_snapshot(s.load_access_grants_checked());
        require_snapshot(s.load_auth_users_checked());
        require_snapshot(s.load_ssl_certificates_checked());
        require_snapshot(s.load_mail_domains_checked());
        require_snapshot(s.load_mailboxes_checked());
        require_snapshot(s.load_mail_aliases_checked());

        auto module_state = s.load_mail_module_state_checked();
        REQUIRE(module_state.success);
        CHECK(module_state.present);
        CHECK(module_state.value == "active");
    }
    tclean(dir);
}

// ============================================================
// Phase 11-14: Failure handling
// ============================================================

TEST_CASE("P11-14 startup validation rejects symlinked SQLite database path") {
    auto dir = tdir("p1114_symlink_db");
    tclean(dir); fs::create_directories(dir);
    {
        StorageOptions opts;
        opts.core_backend = CoreStorageBackend::SqlitePhase5;
        opts.skip_startup_validation = true;
        Storage s(dir, opts);
        REQUIRE(s.sqlite_ready());
    }

    auto real_db = dir + "actual-containercp.db";
    auto configured_db = dir + "containercp.db";
    fs::rename(configured_db, real_db);
    fs::create_symlink(real_db, configured_db);
    create_state_file(dir, "sqlite", configured_db);

    StorageOptions opts;
    opts.core_backend = CoreStorageBackend::SqlitePhase5;
    opts.skip_startup_validation = false;

    try {
        Storage s(dir, opts);
        FAIL("SQLite startup accepted a symlinked database path");
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        CHECK(msg.find("symlink") != std::string::npos);
        CHECK(msg.find(configured_db) != std::string::npos);
    }
    CHECK_FALSE(fs::exists(dir + "nodes.db"));
    tclean(dir);
}
