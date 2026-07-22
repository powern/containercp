#include "doctest/doctest.h"

#include "sqlconsole/DatabaseSqlConsoleService.h"
#include "sqlconsole/SqlConsoleAudit.h"
#include "sqlconsole/SqlConsoleSessionStore.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

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

class FakeSqlConsoleProvider : public containercp::database::DatabaseProvider {
public:
    bool fail_create_temporary = false;
    bool fail_drop_temporary = false;
    mutable bool temporary_created = false;
    mutable bool temporary_dropped = false;
    mutable std::string created_database;
    mutable std::string created_user;
    mutable std::string created_password;
    mutable std::string dropped_user;

    containercp::database::DatabaseProviderResult ok(std::string code = "ok") const { return {true, code, "ok", {}}; }
    containercp::database::DatabaseProviderResult no(std::string code = "failed") const { return {false, code, "safe failure", {}}; }

    containercp::database::DatabaseProviderResult verify_service_account(const containercp::database::MariaDBConnectionTarget&, const containercp::database::DatabaseProviderCredential&) const override { return ok(); }
    containercp::database::DatabaseProviderResult database_exists(const containercp::database::MariaDBConnectionTarget&, const containercp::database::DatabaseProviderCredential&, const std::string&) const override { return ok(); }
    containercp::database::DatabaseProviderResult user_exists(const containercp::database::MariaDBConnectionTarget&, const containercp::database::DatabaseProviderCredential&, const std::string&) const override { return ok(); }
    containercp::database::DatabaseProviderResult user_schema_grant_count(const containercp::database::MariaDBConnectionTarget&, const containercp::database::DatabaseProviderCredential&, const std::string&) const override { return ok(); }
    containercp::database::DatabaseProviderResult create_database(const containercp::database::MariaDBConnectionTarget&, const containercp::database::DatabaseProviderCredential&, const std::string&) const override { return ok(); }
    containercp::database::DatabaseProviderResult create_or_update_user(const containercp::database::MariaDBConnectionTarget&, const containercp::database::DatabaseProviderCredential&, const std::string&, const std::string&) const override { return ok(); }
    containercp::database::DatabaseProviderResult grant_database_privileges(const containercp::database::MariaDBConnectionTarget&, const containercp::database::DatabaseProviderCredential&, const std::string&, const std::string&) const override { return ok(); }
    containercp::database::DatabaseProviderResult create_temporary_sql_console_user(const containercp::database::MariaDBConnectionTarget&, const containercp::database::DatabaseProviderCredential&, const std::string& database_name, const std::string& user_name, const std::string& password) const override {
        temporary_created = true;
        created_database = database_name;
        created_user = user_name;
        created_password = password;
        return fail_create_temporary ? no("temporary_create_failed") : ok("temporary_sql_console_user_ready");
    }
    containercp::database::DatabaseProviderResult drop_temporary_sql_console_user(const containercp::database::MariaDBConnectionTarget&, const containercp::database::DatabaseProviderCredential&, const std::string&, const std::string& user_name) const override {
        temporary_dropped = true;
        dropped_user = user_name;
        return fail_drop_temporary ? no("temporary_drop_failed") : ok("temporary_sql_console_user_dropped");
    }
    containercp::database::DatabaseProviderResult revoke_database_privileges(const containercp::database::MariaDBConnectionTarget&, const containercp::database::DatabaseProviderCredential&, const std::string&, const std::string&) const override { return ok(); }
    containercp::database::DatabaseProviderResult drop_database(const containercp::database::MariaDBConnectionTarget&, const containercp::database::DatabaseProviderCredential&, const std::string&) const override { return ok(); }
    containercp::database::DatabaseProviderResult drop_user(const containercp::database::MariaDBConnectionTarget&, const containercp::database::DatabaseProviderCredential&, const std::string&) const override { return ok(); }
    containercp::database::DatabaseProviderResult verify_login(const containercp::database::MariaDBConnectionTarget&, const std::string&, const std::string&, const std::string&) const override { return ok(); }
    containercp::database::DatabaseProviderResult export_database(const containercp::database::MariaDBConnectionTarget&, const std::string&, const std::string&, const std::string&, const std::string&) const override { return ok(); }
    containercp::database::DatabaseProviderResult import_sql_file(const containercp::database::MariaDBConnectionTarget&, const std::string&, const std::string&, const std::string&, const std::string&) const override { return ok(); }
};

SqlConsoleProvisionRequest test_provision_request() {
    SqlConsoleProvisionRequest request;
    request.launch = test_request();
    request.target = {"/srv/containercp/sites/example.test/docker-compose.yml", "mariadb"};
    request.service_account = {"containercp_service", "service-secret", "localhost"};
    request.database_name = "app_db";
    return request;
}

std::filesystem::path test_store_path(const std::string& name) {
    const auto root = std::filesystem::temp_directory_path() / ("containercp-sql-console-store-" + std::to_string(::getpid()) + "-" + name);
    std::filesystem::create_directories(root);
    return root / "sessions.db";
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

TEST_CASE("SQL Console launch cookie authorization redeems then touches session") {
    DatabaseSqlConsoleService service(test_policy());
    const auto created = service.create_launch_session(test_request());
    REQUIRE(created.success);

    const auto first = service.authorize_launch_session(created.launch_id, created.launch_secret);
    REQUIRE(first.success);
    CHECK(first.code == "redeemed");
    CHECK(first.session.status == "redeemed");

    const auto second = service.authorize_launch_session(created.launch_id, created.launch_secret);
    REQUIRE(second.success);
    CHECK(second.code == "touched");
    CHECK(second.session.status == "redeemed");

    const auto invalid = service.authorize_launch_session(created.launch_id, "wrong-secret");
    CHECK_FALSE(invalid.success);
    CHECK(invalid.code == "invalid_secret");
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

TEST_CASE("SQL Console provisioned launch creates temporary database user without public leakage") {
    FakeSqlConsoleProvider provider;
    DatabaseSqlConsoleService service(provider, test_policy());

    const auto created = service.create_temporary_launch_session(test_provision_request());

    REQUIRE(created.success);
    CHECK(provider.temporary_created);
    CHECK(provider.created_database == "app_db");
    CHECK(provider.created_user == created.temporary_credential.user_name);
    CHECK(provider.created_user.rfind("ccp_sql_", 0) == 0);
    CHECK(provider.created_user.size() == 32);
    CHECK(provider.created_password == created.temporary_credential.password);
    CHECK(created.temporary_credential.password.size() == 64);

    const auto json = sql_console_public_session_json(created.session);
    CHECK(json.find(created.temporary_credential.user_name) == std::string::npos);
    CHECK(json.find(created.temporary_credential.password) == std::string::npos);
    CHECK(json.find("temporary") == std::string::npos);
    CHECK(json.find("password") == std::string::npos);
}

TEST_CASE("SQL Console provisioned revoke drops temporary database user before revoking") {
    FakeSqlConsoleProvider provider;
    DatabaseSqlConsoleService service(provider, test_policy());
    const auto created = service.create_temporary_launch_session(test_provision_request());
    REQUIRE(created.success);

    SqlConsoleCleanupRequest cleanup;
    cleanup.launch_id = created.launch_id;
    cleanup.target = test_provision_request().target;
    cleanup.service_account = test_provision_request().service_account;
    const auto revoked = service.revoke_temporary_launch_session(cleanup);

    REQUIRE(revoked.success);
    CHECK(revoked.session.status == "revoked");
    CHECK(provider.temporary_dropped);
    CHECK(provider.dropped_user == created.temporary_credential.user_name);
    const auto* internal = service.sessions().find(created.launch_id);
    REQUIRE(internal != nullptr);
    CHECK(internal->temporary_user_name.empty());
    CHECK(internal->temporary_user_password.empty());
}

TEST_CASE("SQL Console temporary user creation failure fails closed") {
    FakeSqlConsoleProvider provider;
    provider.fail_create_temporary = true;
    DatabaseSqlConsoleService service(provider, test_policy());

    const auto created = service.create_temporary_launch_session(test_provision_request());

    CHECK_FALSE(created.success);
    CHECK(created.code == "temporary_create_failed");
    CHECK(created.launch_secret.empty());
    const auto* internal = service.sessions().find(created.launch_id);
    REQUIRE(internal != nullptr);
    CHECK(internal->status == SqlConsoleSessionStatus::Revoked);
}

TEST_CASE("SQL Console session store persists non-secret metadata only") {
    const auto path = test_store_path("safe");
    SqlConsoleSessionStore store(path);
    FakeSqlConsoleProvider provider;
    DatabaseSqlConsoleService service(provider, store, test_policy());

    const auto created = service.create_temporary_launch_session(test_provision_request());
    REQUIRE(created.success);

    std::ifstream in(path);
    REQUIRE(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content.find(created.launch_id) != std::string::npos);
    CHECK(content.find(created.temporary_credential.user_name) != std::string::npos);
    CHECK(content.find(created.launch_secret) == std::string::npos);
    CHECK(content.find(created.temporary_credential.password) == std::string::npos);
    CHECK(content.find("secret_digest") == std::string::npos);
    CHECK(content.find("service-secret") == std::string::npos);

    const auto loaded = store.load();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].launch_id == created.launch_id);
    CHECK(loaded[0].temporary_user_name == created.temporary_credential.user_name);
    CHECK(loaded[0].temporary_user_password.empty());
    CHECK(loaded[0].secret_digest.empty());
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("SQL Console recovery drops persisted active temporary users and fails closed") {
    const auto path = test_store_path("recovery");
    SqlConsoleSessionStore store(path);
    FakeSqlConsoleProvider provider;
    {
        DatabaseSqlConsoleService service(provider, store, test_policy());
        const auto created = service.create_temporary_launch_session(test_provision_request());
        REQUIRE(created.success);
    }

    DatabaseSqlConsoleService restarted(provider, store, test_policy());
    const auto recovered = restarted.recover_persisted_sessions([](const SqlConsoleSession&) {
        return SqlConsoleRecoveryTarget{{"/srv/containercp/sites/example.test/docker-compose.yml", "mariadb"}, {"containercp_service", "service-secret", "localhost"}};
    });

    REQUIRE(recovered.success);
    CHECK(recovered.inspected == 1);
    CHECK(recovered.cleaned == 1);
    CHECK(provider.temporary_dropped);
    const auto loaded = store.load();
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0].status == SqlConsoleSessionStatus::Revoked);
    CHECK(loaded[0].temporary_user_name.empty());
    CHECK(loaded[0].temporary_user_password.empty());
    std::filesystem::remove_all(path.parent_path());
}

TEST_CASE("SQL Console internal redemption returns temporary credentials only after valid launch secret") {
    FakeSqlConsoleProvider provider;
    DatabaseSqlConsoleService service(provider, test_policy());
    const auto created = service.create_temporary_launch_session(test_provision_request());
    REQUIRE(created.success);

    const auto invalid = service.redeem_internal_launch_session(created.launch_id, "wrong-secret");
    CHECK_FALSE(invalid.success);
    CHECK(invalid.temporary_credential.password.empty());

    const auto redeemed = service.redeem_internal_launch_session(created.launch_id, created.launch_secret);
    REQUIRE(redeemed.success);
    CHECK(redeemed.session.status == "redeemed");
    CHECK(redeemed.temporary_credential.database_name == "app_db");
    CHECK(redeemed.temporary_credential.user_name == created.temporary_credential.user_name);
    CHECK(redeemed.temporary_credential.password == created.temporary_credential.password);

    const auto public_json = sql_console_public_session_json(redeemed.session);
    CHECK(public_json.find(redeemed.temporary_credential.password) == std::string::npos);
    CHECK(public_json.find(redeemed.temporary_credential.user_name) == std::string::npos);
}
