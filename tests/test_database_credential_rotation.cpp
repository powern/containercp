#include "database/DatabaseCredentialRotationService.h"

#include "doctest/doctest.h"

#include <thread>
#include <vector>

using namespace containercp::database;

namespace {

struct FakeRotationDependencies : DatabaseCredentialRotationDependencies {
    std::vector<std::string> calls;
    std::string generated = "new-secret";
    std::string fail_call;
    std::string seen_password;

    DatabaseCredentialRotationStepResult ok(const std::string& call) {
        calls.push_back(call);
        if (call == fail_call) {
            return {false, call + "_failed", "failure with secret new-secret", ""};
        }
        return {true, "ok", "ok", ""};
    }

    DatabaseCredentialRotationStepResult inspect_wordpress(const DatabaseCredentialRotationRequest&) override {
        return ok("inspect_wordpress");
    }
    DatabaseCredentialRotationStepResult verify_old_credential(const DatabaseCredentialRotationRequest&) override {
        return ok("verify_old_credential");
    }
    DatabaseCredentialRotationStepResult generate_password(const DatabaseCredentialRotationRequest&) override {
        calls.push_back("generate_password");
        if (fail_call == "generate_password") {
            return {false, "generate_password_failed", "failure with secret new-secret", ""};
        }
        return {true, "ok", "ok", generated};
    }
    DatabaseCredentialRotationStepResult change_mariadb_password(const DatabaseCredentialRotationRequest&, const std::string& new_password) override {
        seen_password = new_password;
        return ok("change_mariadb_password");
    }
    DatabaseCredentialRotationStepResult update_wordpress_config(const DatabaseCredentialRotationRequest&, const std::string& new_password) override {
        seen_password = new_password;
        return ok("update_wordpress_config");
    }
    DatabaseCredentialRotationStepResult apply_runtime(const DatabaseCredentialRotationRequest&) override {
        return ok("apply_runtime");
    }
    DatabaseCredentialRotationStepResult verify_new_credential(const DatabaseCredentialRotationRequest&, const std::string& new_password) override {
        seen_password = new_password;
        return ok("verify_new_credential");
    }
    DatabaseCredentialRotationStepResult verify_wordpress(const DatabaseCredentialRotationRequest&) override {
        return ok("verify_wordpress");
    }
    DatabaseCredentialRotationStepResult verify_site_health(const DatabaseCredentialRotationRequest&) override {
        return ok("verify_site_health");
    }
    DatabaseCredentialRotationStepResult persist_metadata(const DatabaseCredentialRotationRequest&, const std::string& new_password) override {
        seen_password = new_password;
        return ok("persist_metadata");
    }
};

} // namespace

TEST_CASE("DatabaseCredentialRotationService state strings are stable") {
    CHECK(database_credential_rotation_state_to_string(DatabaseCredentialRotationState::NotStarted) == "not_started");
    CHECK(database_credential_rotation_state_to_string(DatabaseCredentialRotationState::LockAcquired) == "lock_acquired");
    CHECK(database_credential_rotation_state_to_string(DatabaseCredentialRotationState::Compensating) == "compensating");
    CHECK(database_credential_rotation_state_to_string(DatabaseCredentialRotationState::ManualRecoveryRequired) == "manual_recovery_required");
    CHECK(database_credential_rotation_state_to_string(DatabaseCredentialRotationState::Completed) == "completed");
}

TEST_CASE("DatabaseCredentialRotationService rejects site_id zero") {
    DatabaseCredentialRotationService service;
    const auto result = service.rotate({0, 1, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.final_state == DatabaseCredentialRotationState::Failed);
    CHECK(result.code == "system_site_unsupported");
    CHECK_FALSE(service.is_locked(0, 1));
}

TEST_CASE("DatabaseCredentialRotationService rejects missing database id") {
    DatabaseCredentialRotationService service;
    const auto result = service.rotate({1, 0, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "database_required");
    CHECK_FALSE(service.is_locked(1, 0));
}

TEST_CASE("DatabaseCredentialRotationService releases lock after failure") {
    DatabaseCredentialRotationService service;
    const auto result = service.rotate({1, 2, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "rotation_dependencies_missing");
    CHECK_FALSE(service.is_locked(1, 2));
    REQUIRE(result.events.size() >= 3);
    CHECK(result.events[0].state == DatabaseCredentialRotationState::LockAcquired);
    CHECK(result.events[1].state == DatabaseCredentialRotationState::InspectingWordPress);
    CHECK(result.events.back().state == DatabaseCredentialRotationState::Failed);
}

TEST_CASE("DatabaseCredentialRotationService events do not expose confirmation text") {
    DatabaseCredentialRotationService service;
    const auto result = service.rotate({1, 2, "secret-domain-confirmation"});

    for (const auto& e : result.events) {
        CHECK(e.message.find("secret-domain-confirmation") == std::string::npos);
        CHECK(e.code.find("secret-domain-confirmation") == std::string::npos);
    }
    CHECK(result.message.find("secret-domain-confirmation") == std::string::npos);
}

TEST_CASE("DatabaseCredentialRotationService executes happy path in order") {
    FakeRotationDependencies deps;
    deps.generated = "generated-secret";
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK(result.success);
    CHECK(result.final_state == DatabaseCredentialRotationState::Completed);
    CHECK(result.code == "completed");
    CHECK(deps.seen_password == "generated-secret");
    CHECK(deps.calls == std::vector<std::string>{
        "inspect_wordpress",
        "verify_old_credential",
        "generate_password",
        "change_mariadb_password",
        "update_wordpress_config",
        "apply_runtime",
        "verify_new_credential",
        "verify_wordpress",
        "verify_site_health",
        "persist_metadata",
    });
    CHECK_FALSE(service.is_locked(10, 20));
}

TEST_CASE("DatabaseCredentialRotationService rejects unsupported inspection before mutation") {
    FakeRotationDependencies deps;
    deps.fail_call = "inspect_wordpress";
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "inspect_wordpress_failed");
    CHECK(result.message.find("new-secret") == std::string::npos);
    for (const auto& event : result.events) {
        CHECK(event.message.find("new-secret") == std::string::npos);
    }
    CHECK(deps.calls == std::vector<std::string>{"inspect_wordpress"});
    CHECK_FALSE(service.is_locked(10, 20));
}

TEST_CASE("DatabaseCredentialRotationService passes generated password only to dependency calls") {
    FakeRotationDependencies deps;
    deps.generated = "super-secret-generated";
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK(result.success);
    CHECK(deps.seen_password == "super-secret-generated");
    for (const auto& event : result.events) {
        CHECK(event.code.find("super-secret-generated") == std::string::npos);
        CHECK(event.message.find("super-secret-generated") == std::string::npos);
    }
    CHECK(result.message.find("super-secret-generated") == std::string::npos);
}

TEST_CASE("DatabaseCredentialRotationService stops before metadata when verification fails") {
    FakeRotationDependencies deps;
    deps.fail_call = "verify_new_credential";
    DatabaseCredentialRotationService service(deps);

    const auto result = service.rotate({10, 20, "example.com"});

    CHECK_FALSE(result.success);
    CHECK(result.code == "verify_new_credential_failed");
    CHECK(deps.calls == std::vector<std::string>{
        "inspect_wordpress",
        "verify_old_credential",
        "generate_password",
        "change_mariadb_password",
        "update_wordpress_config",
        "apply_runtime",
        "verify_new_credential",
    });
}
