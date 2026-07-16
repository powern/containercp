#include "storage/Storage.h"
#include "storage/SQLiteStorage.h"
#include "storage/SchemaMigrations.h"

#include <filesystem>
#include <string>

#include "doctest/doctest.h"

namespace fs = std::filesystem;

static std::string tdir(const std::string& name) {
    return (fs::temp_directory_path() / name).string() + "/";
}

static void tclean(const std::string& dir) {
    fs::remove_all(dir);
}

// ============================================================
// SQLiteStorage direct tests
// ============================================================

// Helper: initialise a pool and run schema migration in a given directory.
// The database file is <dir>/test.db.
static void init_pool(containercp::storage::ConnectionPool& pool, const std::string& dir) {
    REQUIRE(pool.initialize(dir + "test.db"));
    containercp::storage::SQLiteDB migrator;
    REQUIRE(migrator.open(dir + "test.db"));
    containercp::storage::MigrationEngine eng;
    containercp::storage::register_all_schema_migrations(eng);
    REQUIRE(eng.migrate(migrator));
    migrator.close();
}

TEST_CASE("SQLiteStorage nodes empty load") {
    auto dir = tdir("ss_nodes_empty");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        auto nodes = ss.load_nodes();
        CHECK(nodes.empty());
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage nodes round trip") {
    auto dir = tdir("ss_nodes_rt");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);

        containercp::node::Node n;
        n.id = 1; n.name = "local"; n.type = "local";
        ss.save_nodes({n});

        auto loaded = ss.load_nodes();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].id == 1);
        CHECK(loaded[0].name == "local");
        CHECK(loaded[0].type == "local");
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage nodes preserve non-contiguous IDs") {
    auto dir = tdir("ss_nodes_ncid");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);

        containercp::node::Node n1, n2;
        n1.id = 1; n1.name = "first"; n1.type = "local";
        n2.id = 8; n2.name = "eighth"; n2.type = "local";
        ss.save_nodes({n1, n2});

        auto loaded = ss.load_nodes();
        REQUIRE(loaded.size() == 2);
        CHECK(loaded[0].id == 1);
        CHECK(loaded[1].id == 8);
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage nodes update via full replacement") {
    auto dir = tdir("ss_nodes_upd");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);

        containercp::node::Node n;
        n.id = 1; n.name = "original"; n.type = "local";
        ss.save_nodes({n});

        n.name = "updated";
        ss.save_nodes({n});

        auto loaded = ss.load_nodes();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].name == "updated");
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage nodes removal via replacement") {
    auto dir = tdir("ss_nodes_rem");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);

        containercp::node::Node n1, n2;
        n1.id = 1; n1.name = "keep"; n1.type = "local";
        n2.id = 2; n2.name = "remove"; n2.type = "local";
        ss.save_nodes({n1, n2});

        // Replace with only n1 — n2 should be removed
        ss.save_nodes({n1});

        auto loaded = ss.load_nodes();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].id == 1);
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage php_versions empty load") {
    auto dir = tdir("ss_php_empty");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        CHECK(ss.load_php_versions().empty());
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage php_versions round trip") {
    auto dir = tdir("ss_php_rt");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);

        containercp::php::PhpVersion pv;
        pv.id = 1; pv.version = "8.4";
        pv.image = "php:8.4-fpm";
        pv.enabled = true;
        pv.default_version = true;
        ss.save_php_versions({pv});

        auto loaded = ss.load_php_versions();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].id == 1);
        CHECK(loaded[0].version == "8.4");
        CHECK(loaded[0].image == "php:8.4-fpm");
        CHECK(loaded[0].enabled == true);
        CHECK(loaded[0].default_version == true);
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage php_versions flags preserved") {
    auto dir = tdir("ss_php_flags");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);

        containercp::php::PhpVersion pv;
        pv.id = 1; pv.version = "8.2";
        pv.image = "php:8.2-fpm";
        pv.enabled = false;     // disabled
        pv.default_version = false;
        ss.save_php_versions({pv});

        auto loaded = ss.load_php_versions();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].enabled == false);
        CHECK(loaded[0].default_version == false);
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage profiles empty load") {
    auto dir = tdir("ss_prof_empty");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);
        CHECK(ss.load_profiles().empty());
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage profiles round trip") {
    auto dir = tdir("ss_prof_rt");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);

        containercp::profile::Profile p;
        p.id = 1;
        p.profile_name = "test-profile";
        p.type = containercp::profile::ProfileType::WEB_SERVER;
        p.web_server = "nginx";
        p.runtime = "docker";
        p.template_path = "/etc/templates/test.conf";
        p.description = "Test profile";
        p.enabled = true;
        p.default_profile = true;
        ss.save_profiles({p});

        auto loaded = ss.load_profiles();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].id == 1);
        CHECK(loaded[0].profile_name == "test-profile");
        CHECK(loaded[0].web_server == "nginx");
        CHECK(loaded[0].template_path == "/etc/templates/test.conf");
        CHECK(loaded[0].description == "Test profile");
        CHECK(loaded[0].enabled == true);
        CHECK(loaded[0].default_profile == true);
    }
    tclean(dir);
}

TEST_CASE("SQLiteStorage profiles special characters") {
    auto dir = tdir("ss_prof_spec");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::ConnectionPool pool;
        init_pool(pool, dir);
        containercp::storage::SQLiteStorage ss(pool);

        containercp::profile::Profile p;
        p.id = 1;
        p.profile_name = "quote's & \"double\" \\backslash |pipe";
        p.web_server = "nginx";
        p.template_path = "/path/with/unicode/✓";
        p.description = "Multi\nline\tdescription";
        p.enabled = true;
        p.default_profile = false;
        ss.save_profiles({p});

        auto loaded = ss.load_profiles();
        REQUIRE(loaded.size() == 1);
        CHECK(loaded[0].profile_name == p.profile_name);
        CHECK(loaded[0].template_path == p.template_path);
        CHECK(loaded[0].description == p.description);
    }
    tclean(dir);
}

// ============================================================
// Storage dual-backend integration tests
// ============================================================

TEST_CASE("Storage nodes use SQLite, sites use TXT") {
    auto dir = tdir("ss_dual_nodes");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::Storage s(dir);

        containercp::node::Node n;
        n.id = 1; n.name = "test"; n.type = "local";
        s.save_nodes({n});

        containercp::site::Site site;
        site.id = 1; site.domain = "test.com"; site.owner = "admin";
        site.node_id = 1; site.web_server = "apache";
        s.save_sites({site});

        // Nodes from SQLite
        auto nodes = s.load_nodes();
        CHECK(nodes.size() == 1);

        // Sites from TXT
        auto sites = s.load_sites();
        CHECK(sites.size() == 1);
    }
    tclean(dir);
}

TEST_CASE("Storage php_versions use SQLite") {
    auto dir = tdir("ss_dual_php");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::Storage s(dir);

        containercp::php::PhpVersion pv;
        pv.id = 1; pv.version = "8.4";
        pv.image = "php:8.4";
        pv.enabled = true; pv.default_version = true;
        s.save_php_versions({pv});

        auto loaded = s.load_php_versions();
        CHECK(loaded.size() == 1);
    }
    tclean(dir);
}

TEST_CASE("Storage profiles use SQLite") {
    auto dir = tdir("ss_dual_prof");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::Storage s(dir);

        containercp::profile::Profile p;
        p.id = 1; p.profile_name = "test";
        p.web_server = "apache";
        s.save_profiles({p});

        auto loaded = s.load_profiles();
        CHECK(loaded.size() == 1);
    }
    tclean(dir);
}

TEST_CASE("Storage SQLite save does not affect TXT files") {
    auto dir = tdir("ss_no_cross");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::Storage s(dir);

        // Save node (SQLite)
        containercp::node::Node n;
        n.id = 1; n.name = "n"; n.type = "local";
        s.save_nodes({n});

        // TXT files for non-migrated types should not exist
        CHECK_FALSE(fs::exists(dir + "auth_users.db"));
    }
    tclean(dir);
}

TEST_CASE("Storage reopening reloads committed SQLite state") {
    auto dir = tdir("ss_reopen");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::Storage s1(dir);
        containercp::node::Node n;
        n.id = 42; n.name = "persist"; n.type = "local";
        s1.save_nodes({n});
    }
    {
        containercp::storage::Storage s2(dir);
        auto nodes = s2.load_nodes();
        REQUIRE(nodes.size() == 1);
        CHECK(nodes[0].id == 42);
        CHECK(nodes[0].name == "persist");
    }
    tclean(dir);
}

TEST_CASE("Storage existing TXT tests still pass") {
    // Verify that TXT-backed Storage methods still work
    auto dir = tdir("ss_txt_work");
    tclean(dir); fs::create_directories(dir);
    {
        containercp::storage::Storage s(dir);

        containercp::auth::AuthUser u;
        u.id = 1; u.username = "admin";
        u.password_hash = "hash";
        u.role = "admin";
        s.save_auth_users({u});

        auto loaded = s.load_auth_users();
        CHECK(loaded.size() == 1);
    }
    tclean(dir);
}
