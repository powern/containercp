#include "storage/Storage.h"
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
