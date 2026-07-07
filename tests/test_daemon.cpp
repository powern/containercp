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
