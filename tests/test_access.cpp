#include "access/AccessUserManager.h"
#include "access/AccessGrantManager.h"

#include <cstdint>
#include <string>
#include <vector>

#include "doctest/doctest.h"

TEST_CASE("AccessUserManager create/find/list/remove") {
    containercp::access::AccessUserManager mgr;

    uint64_t id = mgr.create("devuser");
    CHECK(id == 1);

    auto* u = mgr.find("devuser");
    REQUIRE(u != nullptr);
    CHECK(u->username == "devuser");
    CHECK(u->auth_type == "password");
    CHECK(u->enabled);

    CHECK(mgr.list().size() == 1);

    CHECK(mgr.remove(id));
    CHECK(mgr.find("devuser") == nullptr);
    CHECK(mgr.list().empty());
}

TEST_CASE("AccessUserManager enable/disable") {
    containercp::access::AccessUserManager mgr;
    mgr.create("editor");

    auto* u = mgr.find("editor");
    REQUIRE(u != nullptr);
    CHECK(u->enabled);

    u->enabled = false;
    CHECK_FALSE(u->enabled);

    u->enabled = true;
    CHECK(u->enabled);
}

TEST_CASE("AccessGrantManager create/find/list/remove") {
    containercp::access::AccessGrantManager mgr;

    uint64_t gid = mgr.create(1, 100, containercp::access::Permission::READ_WRITE);
    CHECK(gid == 1);

    auto* g = mgr.find(1);
    REQUIRE(g != nullptr);
    CHECK(g->access_user_id == 1);
    CHECK(g->site_id == 100);
    CHECK(g->permission == containercp::access::Permission::READ_WRITE);

    CHECK(mgr.list().size() == 1);

    auto by_user = mgr.find_by_user(1);
    CHECK(by_user.size() == 1);
    CHECK(by_user[0]->site_id == 100);

    CHECK(mgr.remove(gid));
    CHECK(mgr.find(1) == nullptr);
}

TEST_CASE("AccessGrantManager multiple grants") {
    containercp::access::AccessGrantManager mgr;

    mgr.create(1, 100, containercp::access::Permission::READ_ONLY);
    mgr.create(1, 200, containercp::access::Permission::DEPLOY);
    mgr.create(2, 100, containercp::access::Permission::READ_WRITE);

    CHECK(mgr.list().size() == 3);

    auto user1_grants = mgr.find_by_user(1);
    CHECK(user1_grants.size() == 2);

    auto site100_grants = mgr.find_by_site(100);
    CHECK(site100_grants.size() == 2);
}

TEST_CASE("AccessGrantManager permissions") {
    using containercp::access::Permission;
    CHECK(containercp::access::permission_to_string(Permission::READ_ONLY) == "read_only");
    CHECK(containercp::access::permission_to_string(Permission::READ_WRITE) == "read_write");
    CHECK(containercp::access::permission_to_string(Permission::DEPLOY) == "deploy");

    CHECK(containercp::access::permission_from_string("read_only") == Permission::READ_ONLY);
    CHECK(containercp::access::permission_from_string("read_write") == Permission::READ_WRITE);
    CHECK(containercp::access::permission_from_string("deploy") == Permission::DEPLOY);
    CHECK(containercp::access::permission_from_string("unknown") == Permission::READ_ONLY);
}
