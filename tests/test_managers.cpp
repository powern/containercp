#include "user/UserManager.h"
#include "site/SiteManager.h"

#include <cstdint>
#include <string>

#include "doctest/doctest.h"

TEST_CASE("UserManager create/find/list/remove") {
    containercp::user::UserManager mgr;

    uint64_t id = mgr.create("testuser", 1001, "/home/testuser", "/bin/bash");
    CHECK(id == 1);

    auto* u = mgr.find("testuser");
    REQUIRE(u != nullptr);
    CHECK(u->username == "testuser");
    CHECK(u->uid == 1001);
    CHECK(u->home_directory == "/home/testuser");
    CHECK(u->shell == "/bin/bash");
    CHECK(u->enabled);

    CHECK(mgr.list().size() == 1);

    CHECK(mgr.remove(id));
    CHECK(mgr.find("testuser") == nullptr);
    CHECK(mgr.list().empty());

    CHECK_FALSE(mgr.remove(999));
}

TEST_CASE("UserManager duplicate name") {
    containercp::user::UserManager mgr;
    mgr.create("dup", 1001, "/home/dup", "/bin/bash");
    mgr.create("dup", 1002, "/home/dup2", "/bin/bash");
    CHECK(mgr.list().size() == 2);
    auto* u = mgr.find("dup");
    REQUIRE(u != nullptr);
}

TEST_CASE("SiteManager create/find/list/remove") {
    containercp::site::SiteManager mgr;

    uint64_t id = mgr.create("example.com", "admin", 1);
    CHECK(id == 1);

    auto* s = mgr.find("example.com");
    REQUIRE(s != nullptr);
    CHECK(s->domain == "example.com");
    CHECK(s->owner == "admin");
    CHECK(s->node_id == 1);
    CHECK(s->id == 1);

    CHECK(mgr.list().size() == 1);

    CHECK(mgr.remove(id));
    CHECK(mgr.find("example.com") == nullptr);
    CHECK(mgr.list().empty());
}

TEST_CASE("SiteManager find_by_id") {
    containercp::site::SiteManager mgr;
    uint64_t id1 = mgr.create("site1.com", "admin", 1);
    uint64_t id2 = mgr.create("site2.com", "admin", 1);

    auto* s = mgr.find_by_id(id1);
    REQUIRE(s != nullptr);
    CHECK(s->domain == "site1.com");
    CHECK(s->id == id1);

    s = mgr.find_by_id(id2);
    REQUIRE(s != nullptr);
    CHECK(s->domain == "site2.com");
    CHECK(s->id == id2);

    CHECK(mgr.find_by_id(999) == nullptr);
}

TEST_CASE("SiteManager remove cleans state") {
    containercp::site::SiteManager mgr;
    mgr.create("test.com", "admin", 1);
    mgr.create("other.com", "admin", 1);
    CHECK(mgr.list().size() == 2);

    // Remove one site
    auto* s = mgr.find("test.com");
    REQUIRE(s != nullptr);
    mgr.remove(s->id);

    // Only the other site remains
    CHECK(mgr.list().size() == 1);
    CHECK(mgr.find("test.com") == nullptr);
    CHECK(mgr.find("other.com") != nullptr);
}
