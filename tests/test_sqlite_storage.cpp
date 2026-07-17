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

TEST_CASE("Phase6a SQLite resources coexist with TXT resources") {
    auto dir = tdir("s6a_coexist"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());

        // SQLite-backed users
        containercp::user::User u; u.id = 1; u.username = "sqlite_user"; u.enabled = true;
        s.save_users({u});

        // TXT-backed auth_users
        containercp::auth::AuthUser au; au.id = 1; au.username = "admin";
        au.password_hash = "h"; au.role = "admin";
        s.save_auth_users({au});

        CHECK(s.load_users().size() == 1);
        CHECK(s.load_auth_users().size() == 1);
    }
    tclean(dir);
}

TEST_CASE("Phase6a saving sites does not alter users or domains") {
    auto dir = tdir("s6a_isolation"); tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::StorageOptions opts;
        opts.core_backend = containercp::storage::CoreStorageBackend::SqlitePhase5;
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
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());

        containercp::node::Node n; n.id = 1; n.name = "n"; n.type = "local";
        s.save_nodes({n});
        CHECK(s.load_nodes().size() == 1);

        containercp::user::User u; u.id = 1; u.username = "u"; u.enabled = true;
        s.save_users({u});
        CHECK(s.load_users().size() == 1);

        // TXT-backed auth_users unchanged
        containercp::auth::AuthUser au; au.id = 1; au.username = "admin";
        au.password_hash = "h"; au.role = "admin";
        s.save_auth_users({au});
        CHECK(s.load_auth_users().size() == 1);
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
        containercp::storage::Storage s(dir, opts);
        CHECK(s.sqlite_ready());

        containercp::node::Node n; n.id = 1; n.name = "n"; n.type = "local";
        s.save_nodes({n});
        CHECK(s.load_nodes().size() == 1);

        containercp::database::Database db;
        db.id = 1; db.db_name = "d"; db.site_id = 1;
        s.save_databases({db});
        CHECK(s.load_databases().size() == 1);

        // TXT-backed auth_users unchanged
        containercp::auth::AuthUser au;
        au.id = 1; au.username = "admin"; au.password_hash = "h"; au.role = "admin";
        s.save_auth_users({au});
        CHECK(s.load_auth_users().size() == 1);
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
