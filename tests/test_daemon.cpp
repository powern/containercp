#include "daemon/CommandProtocol.h"

#include <string>

#include "doctest/doctest.h"

TEST_CASE("Command encode/decode") {
    containercp::daemon::Command cmd;
    cmd.name = "user-create";
    cmd.args = {"admin", "1001"};

    std::string encoded = cmd.encode();
    CHECK(encoded == "user-create|admin|1001");

    containercp::daemon::Command decoded = containercp::daemon::Command::decode(encoded);
    CHECK(decoded.name == "user-create");
    CHECK(decoded.args.size() == 2);
    CHECK(decoded.args[0] == "admin");
    CHECK(decoded.args[1] == "1001");
}

TEST_CASE("Command success/error wrapping") {
    auto s = containercp::daemon::Command::success("ok");
    CHECK(s == "SUCCESS|ok");
    CHECK(containercp::daemon::Command::is_success(s));

    auto e = containercp::daemon::Command::error("fail");
    CHECK(e == "ERROR|fail");
    CHECK_FALSE(containercp::daemon::Command::is_success(e));
}

TEST_CASE("Command message extraction") {
    CHECK(containercp::daemon::Command::message("SUCCESS|hello") == "hello");
    CHECK(containercp::daemon::Command::message("ERROR|bad") == "bad");
    CHECK(containercp::daemon::Command::message("no-pipe") == "no-pipe");
}

TEST_CASE("Command decode empty") {
    auto cmd = containercp::daemon::Command::decode("");
    CHECK(cmd.name.empty());
    CHECK(cmd.args.empty());
}

TEST_CASE("Command encode with no args") {
    containercp::daemon::Command cmd;
    cmd.name = "site-list";
    CHECK(cmd.encode() == "site-list");
}

TEST_CASE("migrate-to-sqlite command wire format") {
    std::string cmd = "migrate-to-sqlite"
        "|--source|/srv/containercp/database"
        "|--database|/srv/containercp/database/containercp.db"
        "|--archive-root|/srv/containercp/archive"
        "|--source-version|v0.6.0"
        "|--target-version|v0.7.0"
        "|--confirm";

    auto decoded = containercp::daemon::Command::decode(cmd);
    CHECK(decoded.name == "migrate-to-sqlite");
    CHECK(decoded.args.size() == 11);

    // Verify flag-value pairs
    auto it = decoded.args.begin();
    CHECK(*it++ == "--source");         CHECK(*it++ == "/srv/containercp/database");
    CHECK(*it++ == "--database");       CHECK(*it++ == "/srv/containercp/database/containercp.db");
    CHECK(*it++ == "--archive-root");   CHECK(*it++ == "/srv/containercp/archive");
    CHECK(*it++ == "--source-version"); CHECK(*it++ == "v0.6.0");
    CHECK(*it++ == "--target-version"); CHECK(*it++ == "v0.7.0");
    CHECK(*it++ == "--confirm");
}

TEST_CASE("migrate-to-sqlite missing --confirm rejected") {
    std::string cmd = "migrate-to-sqlite"
        "|--source|/tmp/src"
        "|--database|/tmp/db"
        "|--archive-root|/tmp/archive"
        "|--source-version|v0.6.0"
        "|--target-version|v0.7.0";

    auto decoded = containercp::daemon::Command::decode(cmd);
    CHECK(decoded.name == "migrate-to-sqlite");

    bool has_confirm = false;
    for (const auto& arg : decoded.args) {
        if (arg == "--confirm") has_confirm = true;
    }
    CHECK_FALSE(has_confirm);
}
