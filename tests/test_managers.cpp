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

    CHECK(mgr.list().size() == 1);

    CHECK(mgr.remove(id));
    CHECK(mgr.find("example.com") == nullptr);
    CHECK(mgr.list().empty());
}
