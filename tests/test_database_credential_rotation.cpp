#include "database/DatabaseCredentialRotationService.h"

#include "doctest/doctest.h"

#include <thread>

using namespace containercp::database;

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
