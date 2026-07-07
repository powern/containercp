#include "storage/Storage.h"
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
