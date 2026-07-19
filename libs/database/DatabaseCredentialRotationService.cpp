#include "DatabaseCredentialRotationService.h"

#include <utility>

namespace containercp::database {
namespace {

DatabaseCredentialRotationEvent event(DatabaseCredentialRotationState state, std::string code, std::string message) {
    return {state, std::move(code), std::move(message)};
}

} // namespace

std::string database_credential_rotation_state_to_string(DatabaseCredentialRotationState state) {
    switch (state) {
    case DatabaseCredentialRotationState::NotStarted:
        return "not_started";
    case DatabaseCredentialRotationState::LockAcquired:
        return "lock_acquired";
    case DatabaseCredentialRotationState::InspectingWordPress:
        return "inspecting_wordpress";
    case DatabaseCredentialRotationState::VerifyingOldCredential:
        return "verifying_old_credential";
    case DatabaseCredentialRotationState::GeneratingPassword:
        return "generating_password";
    case DatabaseCredentialRotationState::ChangingMariaDBPassword:
        return "changing_mariadb_password";
    case DatabaseCredentialRotationState::UpdatingWordPressConfig:
        return "updating_wordpress_config";
    case DatabaseCredentialRotationState::ApplyingRuntime:
        return "applying_runtime";
    case DatabaseCredentialRotationState::VerifyingMariaDBPassword:
        return "verifying_mariadb_password";
    case DatabaseCredentialRotationState::VerifyingWordPress:
        return "verifying_wordpress";
    case DatabaseCredentialRotationState::VerifyingSiteHealth:
        return "verifying_site_health";
    case DatabaseCredentialRotationState::PersistingMetadata:
        return "persisting_metadata";
    case DatabaseCredentialRotationState::Completed:
        return "completed";
    case DatabaseCredentialRotationState::Compensating:
        return "compensating";
    case DatabaseCredentialRotationState::Compensated:
        return "compensated";
    case DatabaseCredentialRotationState::ManualRecoveryRequired:
        return "manual_recovery_required";
    case DatabaseCredentialRotationState::Failed:
        return "failed";
    }
    return "unknown";
}

DatabaseCredentialRotationService::OperationLock::OperationLock(DatabaseCredentialRotationService& service,
                                                               uint64_t site_id,
                                                               uint64_t database_id)
    : service_(service)
    , key_(DatabaseCredentialRotationService::lock_key(site_id, database_id)) {
    std::lock_guard<std::mutex> guard(service_.mutex_);
    acquired_ = service_.active_locks_.insert(key_).second;
}

DatabaseCredentialRotationService::OperationLock::~OperationLock() {
    if (acquired_) {
        service_.release_lock(key_);
    }
}

std::string DatabaseCredentialRotationService::lock_key(uint64_t site_id, uint64_t database_id) {
    return std::to_string(site_id) + ":" + std::to_string(database_id);
}

void DatabaseCredentialRotationService::release_lock(const std::string& key) {
    std::lock_guard<std::mutex> guard(mutex_);
    active_locks_.erase(key);
}

bool DatabaseCredentialRotationService::is_locked(uint64_t site_id, uint64_t database_id) const {
    std::lock_guard<std::mutex> guard(mutex_);
    return active_locks_.count(lock_key(site_id, database_id)) != 0;
}

DatabaseCredentialRotationResult DatabaseCredentialRotationService::rotate(const DatabaseCredentialRotationRequest& request) {
    DatabaseCredentialRotationResult result;
    if (request.site_id == 0) {
        result.final_state = DatabaseCredentialRotationState::Failed;
        result.code = "system_site_unsupported";
        result.message = "System site credentials cannot be rotated";
        result.events.push_back(event(result.final_state, result.code, result.message));
        return result;
    }
    if (request.database_id == 0) {
        result.final_state = DatabaseCredentialRotationState::Failed;
        result.code = "database_required";
        result.message = "Database id is required";
        result.events.push_back(event(result.final_state, result.code, result.message));
        return result;
    }

    OperationLock lock(*this, request.site_id, request.database_id);
    if (!lock.acquired()) {
        result.final_state = DatabaseCredentialRotationState::Failed;
        result.code = "rotation_already_running";
        result.message = "A credential rotation is already running for this site database";
        result.events.push_back(event(result.final_state, result.code, result.message));
        return result;
    }

    result.events.push_back(event(DatabaseCredentialRotationState::LockAcquired, "lock_acquired", "Rotation lock acquired"));
    result.events.push_back(event(DatabaseCredentialRotationState::InspectingWordPress, "pending", "WordPress inspection dependency is not wired yet"));
    result.final_state = DatabaseCredentialRotationState::Failed;
    result.code = "rotation_dependencies_missing";
    result.message = "Credential rotation dependencies are not wired yet";
    result.events.push_back(event(result.final_state, result.code, result.message));
    return result;
}

} // namespace containercp::database
