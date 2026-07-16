#include "storage/Storage.h"
#include "storage/SQLiteWrapper.h"
#include "auth/AuthUser.h"
#include "auth/sha256.h"
#include "user/User.h"
#include "site/Site.h"
#include "domain/Domain.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "doctest/doctest.h"

// ============================================================
// SQLite wrapper tests
// ============================================================

static std::string test_db_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

static void cleanup(const std::string& path) {
    std::filesystem::remove(path);
    std::filesystem::remove(path + "-wal");
    std::filesystem::remove(path + "-shm");
}

TEST_CASE("SQLiteWrapper open and close") {
    auto path = test_db_path("containercp_test_open.db");
    cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        CHECK(db.open(path));
        CHECK(db.is_open());
        CHECK(db.close());
        CHECK_FALSE(db.is_open());
    }
    cleanup(path);
}

TEST_CASE("SQLiteWrapper double close is safe") {
    auto path = test_db_path("containercp_test_dblclose.db");
    cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));
        CHECK(db.close());
        CHECK(db.close());  // second close should also succeed (idempotent)
    }
    cleanup(path);
}

TEST_CASE("SQLiteWrapper invalid path") {
    containercp::storage::SQLiteDB db;
    // A path in a non-existent directory should fail
    CHECK_FALSE(db.open("/nonexistent_dir_containercp_test/test.db"));
    CHECK_FALSE(db.is_open());
    CHECK_FALSE(db.error_message().empty());
}

TEST_CASE("SQLiteWrapper exec CREATE TABLE and INSERT") {
    auto path = test_db_path("containercp_test_exec.db");
    cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        CHECK(db.exec("CREATE TABLE test (id INTEGER PRIMARY KEY, val TEXT)"));
        CHECK(db.exec("INSERT INTO test VALUES (1, 'hello')"));
        CHECK(db.exec("INSERT INTO test VALUES (2, 'world')"));
    }
    cleanup(path);
}

TEST_CASE("SQLiteWrapper prepare, bind, step SELECT") {
    auto path = test_db_path("containercp_test_select.db");
    cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));
        REQUIRE(db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT, value INTEGER)"));
        REQUIRE(db.exec("INSERT INTO t VALUES (1, 'alpha', 100)"));
        REQUIRE(db.exec("INSERT INTO t VALUES (2, 'beta', 200)"));

        REQUIRE(db.prepare("SELECT id, name, value FROM t WHERE id = ?"));
        REQUIRE(db.bind_int(1, 2));
        REQUIRE(db.step());  // row available
        CHECK(db.column_int(0) == 2);
        CHECK(db.column_text(1) == "beta");
        CHECK(db.column_int(2) == 200);
        CHECK_FALSE(db.step());  // no more rows (DONE)
    }
    cleanup(path);
}

TEST_CASE("SQLiteWrapper bind_text and bind_null") {
    auto path = test_db_path("containercp_test_bind.db");
    cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));
        REQUIRE(db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, label TEXT, descr TEXT)"));

        REQUIRE(db.prepare("INSERT INTO t VALUES (?, ?, ?)"));
        REQUIRE(db.bind_int(1, 1));
        REQUIRE(db.bind_text(2, "hello world"));
        REQUIRE(db.bind_null(3));
        REQUIRE(db.step() == false);  // DONE (no rows returned from INSERT)

        REQUIRE(db.prepare("SELECT id, label, descr FROM t WHERE id = 1"));
        REQUIRE(db.step());
        CHECK(db.column_int(0) == 1);
        CHECK(db.column_text(1) == "hello world");
        CHECK(db.column_is_null(2));
        CHECK_FALSE(db.step());
    }
    cleanup(path);
}

TEST_CASE("SQLiteWrapper transaction commit") {
    auto path = test_db_path("containercp_test_txn_commit.db");
    cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));
        REQUIRE(db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT)"));

        REQUIRE(db.begin_immediate());
        REQUIRE(db.exec("INSERT INTO t VALUES (1, 'committed')"));
        REQUIRE(db.commit());

        // Verify after commit
        REQUIRE(db.prepare("SELECT v FROM t WHERE id = 1"));
        REQUIRE(db.step());
        CHECK(db.column_text(0) == "committed");
    }
    cleanup(path);
}

TEST_CASE("SQLiteWrapper transaction rollback") {
    auto path = test_db_path("containercp_test_txn_rollback.db");
    cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));
        REQUIRE(db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT)"));

        REQUIRE(db.begin_immediate());
        REQUIRE(db.exec("INSERT INTO t VALUES (1, 'rolled_back')"));
        REQUIRE(db.rollback());

        // Verify rollback
        REQUIRE(db.prepare("SELECT COUNT(*) FROM t"));
        REQUIRE(db.step());
        CHECK(db.column_int(0) == 0);
    }
    cleanup(path);
}

TEST_CASE("SQLiteWrapper PRAGMA foreign_keys = ON") {
    auto path = test_db_path("containercp_test_fk.db");
    cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        // Verify PRAGMA value
        REQUIRE(db.prepare("PRAGMA foreign_keys"));
        REQUIRE(db.step());
        CHECK(db.column_int(0) == 1);  // ON

        // Create tables with FK
        REQUIRE(db.exec("CREATE TABLE parent (id INTEGER PRIMARY KEY)"));
        REQUIRE(db.exec("CREATE TABLE child (id INTEGER PRIMARY KEY, pid INTEGER REFERENCES parent(id))"));

        // Insert child with invalid parent should fail
        REQUIRE(db.exec("INSERT INTO parent VALUES (1)"));
        CHECK_FALSE(db.exec("INSERT INTO child VALUES (1, 99)"));  // FK violation
    }
    cleanup(path);
}

TEST_CASE("SQLiteWrapper PRAGMA journal_mode = WAL") {
    auto path = test_db_path("containercp_test_wal.db");
    cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        REQUIRE(db.prepare("PRAGMA journal_mode"));
        REQUIRE(db.step());
        // WAL mode returns "wal" as text, or "delete" if WAL not available
        std::string mode = db.column_text(0);
        CHECK(mode == "wal");
    }
    cleanup(path);
    // WAL mode was verified above; WAL/SHM file existence is not critical
    cleanup(path);
}

TEST_CASE("SQLiteWrapper busy_timeout") {
    auto path = test_db_path("containercp_test_busy.db");
    cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        REQUIRE(db.prepare("PRAGMA busy_timeout"));
        REQUIRE(db.step());
        CHECK(db.column_int(0) == 5000);
    }
    cleanup(path);
}

TEST_CASE("SQLiteWrapper move semantics") {
    auto path = test_db_path("containercp_test_move.db");
    cleanup(path);
    {
        containercp::storage::SQLiteDB db1;
        REQUIRE(db1.open(path));
        CHECK(db1.is_open());

        // Move construct
        containercp::storage::SQLiteDB db2(std::move(db1));
        CHECK(db2.is_open());
        CHECK_FALSE(db1.is_open());  // NOLINT: moved-from is safe

        // Move assign
        containercp::storage::SQLiteDB db3;
        db3 = std::move(db2);
        CHECK(db3.is_open());
        CHECK_FALSE(db2.is_open());  // NOLINT: moved-from is safe
    }
    cleanup(path);
}

TEST_CASE("SQLiteWrapper error after misuse") {
    containercp::storage::SQLiteDB db;
    // Call prepare without open
    CHECK_FALSE(db.prepare("SELECT 1"));
    CHECK_FALSE(db.error_message().empty());
    CHECK(db.error_code() != 0);
}

TEST_CASE("SQLiteWrapper stale error is cleared after success") {
    auto path = test_db_path("containercp_test_stale.db");
    cleanup(path);
    {
        containercp::storage::SQLiteDB db;
        REQUIRE(db.open(path));

        // Cause an error
        CHECK_FALSE(db.exec("INVALID SQL"));

        // Verify error state is set
        CHECK_FALSE(db.error_message().empty());
        CHECK(db.error_code() != 0);

        // Successful operation should clear the stale error
        REQUIRE(db.exec("CREATE TABLE t (id INTEGER)"));
        CHECK(db.error_code() == 0);
        CHECK(db.error_message().empty());
    }
    cleanup(path);
}

TEST_CASE("Storage load from non-existent file") {
    containercp::storage::Storage s("/tmp/nonexistent_dir_containercp_test/");
    auto users = s.load_users();
    CHECK(users.empty());
}

TEST_CASE("Storage user round-trip") {
    std::string tmp = std::filesystem::temp_directory_path() / "containercp_test_users/";
    std::filesystem::create_directories(tmp);
    containercp::storage::Storage s(tmp);

    containercp::user::User u;
    u.id = 1;
    u.name = "admin";
    u.username = "admin";
    u.uid = 1000;
    u.home_directory = "/home/admin";
    u.shell = "/bin/bash";
    u.enabled = true;

    s.save_users({u});
    auto loaded = s.load_users();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].id == 1);
    CHECK(loaded[0].username == "admin");
    CHECK(loaded[0].uid == 1000);
    CHECK(loaded[0].home_directory == "/home/admin");
    CHECK(loaded[0].shell == "/bin/bash");
    CHECK(loaded[0].enabled);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("Storage domain round-trip") {
    std::string tmp = std::filesystem::temp_directory_path() / "containercp_test_domains/";
    std::filesystem::create_directories(tmp);
    containercp::storage::Storage s(tmp);

    containercp::domain::Domain d;
    d.id = 1;
    d.name = "example.com";
    d.fqdn = "example.com";
    d.owner_id = 1;
    d.site_id = 1;
    d.php_version = "8.4";
    d.ssl_enabled = true;
    d.enabled = true;

    s.save_domains({d});
    auto loaded = s.load_domains();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].id == 1);
    CHECK(loaded[0].fqdn == "example.com");
    CHECK(loaded[0].owner_id == 1);
    CHECK(loaded[0].site_id == 1);
    CHECK(loaded[0].php_version == "8.4");
    CHECK(loaded[0].ssl_enabled);
    CHECK(loaded[0].enabled);

    std::filesystem::remove_all(tmp);
}

TEST_CASE("Auth storage creates directory and persists") {
    std::string tmp = std::filesystem::temp_directory_path() / "containercp_test_auth/";
    std::filesystem::remove_all(tmp);

    // Storage must create the directory if it doesn't exist
    containercp::storage::Storage s(tmp);

    // Directory should now exist
    CHECK(std::filesystem::exists(tmp));

    // Save an auth user
    containercp::auth::AuthUser u;
    u.id = 1;
    u.name = "admin";
    u.username = "admin";
    u.password_hash = "abc123def456";
    u.must_change_password = true;
    u.enabled = true;
    u.role = "admin";
    s.save_auth_users({u});

    // File should exist now
    std::string file_path = tmp + "auth_users.db";
    CHECK(std::filesystem::exists(file_path));

    // Load back and verify
    auto loaded = s.load_auth_users();
    CHECK(loaded.size() == 1);
    CHECK(loaded[0].username == "admin");
    CHECK(loaded[0].password_hash == "abc123def456");
    CHECK(loaded[0].must_change_password == true);
    CHECK(loaded[0].enabled == true);
    CHECK(loaded[0].role == "admin");

    // Simulate password change
    loaded[0].password_hash = "newhash789";
    loaded[0].must_change_password = false;
    s.save_auth_users(loaded);

    // Reload and verify must_change_password persisted
    auto reloaded = s.load_auth_users();
    CHECK(reloaded.size() == 1);
    CHECK(reloaded[0].must_change_password == false);
    CHECK(reloaded[0].password_hash == "newhash789");

    std::filesystem::remove_all(tmp);
}

TEST_CASE("Auth user survives simulated restart") {
    std::string tmp = std::filesystem::temp_directory_path() / "containercp_test_auth2/";
    std::filesystem::remove_all(tmp);

    // "First start" — Storage creates directory
    {
        containercp::storage::Storage s(tmp);
        containercp::auth::AuthUser admin;
        admin.id = 1;
        admin.name = "admin";
        admin.username = "admin";
        admin.password_hash = containercp::auth::sha256("temp-password");
        admin.must_change_password = true;
        admin.enabled = true;
        admin.role = "admin";
        s.save_auth_users({admin});
    }

    // "Restart" — new Storage loads from same directory
    {
        containercp::storage::Storage s(tmp);
        auto loaded = s.load_auth_users();
        CHECK(loaded.size() == 1);
        CHECK(loaded[0].username == "admin");
        CHECK(loaded[0].must_change_password == true);

        loaded[0].password_hash = containercp::auth::sha256("new-password");
        loaded[0].must_change_password = false;
        s.save_auth_users(loaded);
    }

    // "Second restart" — verify must_change_password=false survived
    {
        containercp::storage::Storage s(tmp);
        auto loaded = s.load_auth_users();
        CHECK(loaded.size() == 1);
        CHECK(loaded[0].must_change_password == false);
        CHECK(loaded[0].password_hash == containercp::auth::sha256("new-password"));
    }

    std::filesystem::remove_all(tmp);
}
