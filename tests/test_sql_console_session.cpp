#include "doctest/doctest.h"

#include "sqlconsole/DatabaseSqlConsoleService.h"
#include "sqlconsole/SqlConsoleAudit.h"

#include <chrono>
#include <string>

using namespace containercp::sqlconsole;

namespace {

using ClockPoint = std::chrono::system_clock::time_point;

SqlConsoleSessionPolicy test_policy() {
    SqlConsoleSessionPolicy policy;
    policy.absolute_ttl = std::chrono::seconds(60);
    policy.idle_ttl = std::chrono::seconds(10);
    policy.single_use_redemption = true;
    return policy;
}

SqlConsoleCreateRequest test_request() {
    SqlConsoleCreateRequest request;
    request.database_id = 7;
    request.site_id = 3;
    request.admin_username = "admin";
    request.admin_role = "admin";
    request.provider = "adminer";
    return request;
}

} // namespace

TEST_CASE("SQL Console session creation returns only public session plus one-time secret") {
    ClockPoint now{};
    SqlConsoleSessionManager sessions(test_policy());
    sessions.set_clock_for_tests([&] { return now; });

    const auto created = sessions.create(test_request());
    REQUIRE(created.success);
    CHECK(created.code == "created");
    CHECK(created.launch_id.size() == 32);
    CHECK(created.launch_secret.size() == 64);
    CHECK(created.session.launch_id == created.launch_id);
    CHECK(created.session.database_id == 7);
    CHECK(created.session.site_id == 3);
    CHECK(created.session.status == "created");
    CHECK(created.session.created_at == "1970-01-01T00:00:00Z");
    CHECK(created.session.expires_at == "1970-01-01T00:01:00Z");
    CHECK(created.session.idle_expires_at == "1970-01-01T00:00:10Z");

    const auto json = sql_console_public_session_json(created.session);
    CHECK(json.find(created.launch_secret) == std::string::npos);
    CHECK(json.find("secret") == std::string::npos);
    CHECK(json.find("digest") == std::string::npos);
    CHECK(json.find("credential") == std::string::npos);
}

TEST_CASE("SQL Console session lookup marks expired sessions") {
    ClockPoint now{};
    SqlConsoleSessionManager sessions(test_policy());
    sessions.set_clock_for_tests([&] { return now; });

    const auto created = sessions.create(test_request());
    REQUIRE(created.success);
    REQUIRE(sessions.find(created.launch_id) != nullptr);
    CHECK(sessions.find(created.launch_id)->status == SqlConsoleSessionStatus::Created);

    now += std::chrono::seconds(61);
    const auto* expired = sessions.find(created.launch_id);
    REQUIRE(expired != nullptr);
    CHECK(expired->status == SqlConsoleSessionStatus::Expired);
}

TEST_CASE("SQL Console redemption validates secret and is single use") {
    ClockPoint now{};
    SqlConsoleSessionManager sessions(test_policy());
    sessions.set_clock_for_tests([&] { return now; });

    const auto created = sessions.create(test_request());
    REQUIRE(created.success);

    const auto invalid = sessions.redeem(created.launch_id, "wrong-secret");
    CHECK_FALSE(invalid.success);
    CHECK(invalid.code == "invalid_secret");
    CHECK(invalid.session.status == "created");

    const auto redeemed = sessions.redeem(created.launch_id, created.launch_secret);
    REQUIRE(redeemed.success);
    CHECK(redeemed.code == "redeemed");
    CHECK(redeemed.session.status == "redeemed");
    CHECK(redeemed.session.redeemed_at == "1970-01-01T00:00:00Z");
    CHECK(redeemed.session.idle_expires_at == "1970-01-01T00:00:10Z");

    const auto second = sessions.redeem(created.launch_id, created.launch_secret);
    CHECK_FALSE(second.success);
    CHECK(second.code == "session_already_redeemed");
}

TEST_CASE("SQL Console redeemed sessions honor idle and absolute expiry") {
    ClockPoint now{};
    SqlConsoleSessionManager sessions(test_policy());
    sessions.set_clock_for_tests([&] { return now; });

    const auto created = sessions.create(test_request());
    REQUIRE(created.success);
    REQUIRE(sessions.redeem(created.launch_id, created.launch_secret).success);

    now += std::chrono::seconds(8);
    const auto touched = sessions.touch(created.launch_id, created.launch_secret);
    REQUIRE(touched.success);
    CHECK(touched.session.idle_expires_at == "1970-01-01T00:00:18Z");

    now += std::chrono::seconds(9);
    CHECK(sessions.find(created.launch_id)->status == SqlConsoleSessionStatus::Redeemed);

    now += std::chrono::seconds(2);
    const auto* idle_expired = sessions.find(created.launch_id);
    REQUIRE(idle_expired != nullptr);
    CHECK(idle_expired->status == SqlConsoleSessionStatus::Expired);

    now = ClockPoint{};
    SqlConsoleSessionPolicy absolute_policy;
    absolute_policy.absolute_ttl = std::chrono::seconds(60);
    absolute_policy.idle_ttl = std::chrono::seconds(30);
    SqlConsoleSessionManager absolute_sessions(absolute_policy);
    absolute_sessions.set_clock_for_tests([&] { return now; });
    const auto created_absolute = absolute_sessions.create(test_request());
    REQUIRE(created_absolute.success);
    REQUIRE(absolute_sessions.redeem(created_absolute.launch_id, created_absolute.launch_secret).success);
    now += std::chrono::seconds(29);
    REQUIRE(absolute_sessions.touch(created_absolute.launch_id, created_absolute.launch_secret).success);
    now += std::chrono::seconds(29);
    CHECK(absolute_sessions.touch(created_absolute.launch_id, created_absolute.launch_secret).session.idle_expires_at == "1970-01-01T00:01:00Z");
    now += std::chrono::seconds(2);
    CHECK(absolute_sessions.find(created_absolute.launch_id)->status == SqlConsoleSessionStatus::Expired);
}

TEST_CASE("SQL Console sessions can be revoked explicitly") {
    ClockPoint now{};
    SqlConsoleSessionManager sessions(test_policy());
    sessions.set_clock_for_tests([&] { return now; });

    const auto created = sessions.create(test_request());
    REQUIRE(created.success);
    const auto revoked = sessions.revoke(created.launch_id);
    REQUIRE(revoked.success);
    CHECK(revoked.code == "revoked");
    CHECK(revoked.session.status == "revoked");
    CHECK(revoked.session.revoked_at == "1970-01-01T00:00:00Z");

    const auto redeem_after_revoke = sessions.redeem(created.launch_id, created.launch_secret);
    CHECK_FALSE(redeem_after_revoke.success);
    CHECK(redeem_after_revoke.code == "session_revoked");
}

TEST_CASE("SQL Console sweep and listing use safe public serialization") {
    ClockPoint now{};
    DatabaseSqlConsoleService service(test_policy());
    service.sessions().set_clock_for_tests([&] { return now; });

    auto first_request = test_request();
    first_request.database_id = 7;
    auto second_request = test_request();
    second_request.database_id = 8;
    const auto first = service.create_launch_session(first_request);
    const auto second = service.create_launch_session(second_request);
    REQUIRE(first.success);
    REQUIRE(second.success);

    CHECK(service.list_sessions().size() == 2);
    CHECK(service.list_sessions(7).size() == 1);

    now += std::chrono::seconds(61);
    CHECK(service.sweep_expired_sessions() == 2);
    const auto listed = service.list_sessions();
    REQUIRE(listed.size() == 2);
    CHECK(listed[0].status == "expired");
    const auto json = sql_console_public_sessions_json(listed);
    CHECK(json.find(first.launch_secret) == std::string::npos);
    CHECK(json.find(second.launch_secret) == std::string::npos);
    CHECK(json.find("secret") == std::string::npos);
}

TEST_CASE("SQL Console audit format redacts sensitive fragments") {
    SqlConsoleAuditEvent event;
    event.operation = "launch";
    event.stage = "redeem password=supersecret";
    event.result = "failed token=abcdef";
    event.error_code = "credential=hidden";
    event.launch_id = "abc123";
    event.database_id = 7;
    event.site_id = 3;
    event.admin_username = "admin secret=admin-secret";
    event.provider = "adminer";
    event.status = "created";

    const auto formatted = SqlConsoleAuditLogger::format(event);
    CHECK(formatted.find("supersecret") == std::string::npos);
    CHECK(formatted.find("abcdef") == std::string::npos);
    CHECK(formatted.find("hidden") == std::string::npos);
    CHECK(formatted.find("admin-secret") == std::string::npos);
    CHECK(formatted.find("password=<redacted>") != std::string::npos);
    CHECK(formatted.find("token=<redacted>") != std::string::npos);
    CHECK(formatted.find("credential=<redacted>") != std::string::npos);
    CHECK(formatted.find("<redacted>") != std::string::npos);
    CHECK(formatted.find("\n") == std::string::npos);
}
