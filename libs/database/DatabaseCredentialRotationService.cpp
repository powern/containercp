#include "DatabaseCredentialRotationService.h"

#include <utility>

namespace containercp::database {
namespace {

DatabaseCredentialRotationEvent event(DatabaseCredentialRotationState state, std::string code, std::string message) {
    return {state, std::move(code), std::move(message)};
}

DatabaseCredentialRotationResult fail_with(DatabaseCredentialRotationResult result,
                                          DatabaseCredentialRotationState state,
                                          std::string code,
                                          std::string message) {
    result.success = false;
    result.final_state = state;
    result.code = std::move(code);
    result.message = std::move(message);
    result.events.push_back(event(result.final_state, result.code, result.message));
    return result;
}

} // namespace

DatabaseCredentialRotationService::DatabaseCredentialRotationService(DatabaseCredentialRotationDependencies& dependencies)
    : dependencies_(&dependencies) {
}

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

DatabaseCredentialRotationResult DatabaseCredentialRotationService::compensate_after_failure(DatabaseCredentialRotationResult result,
                                                                                             const DatabaseCredentialRotationRequest& request,
                                                                                             const std::string& new_password,
                                                                                             bool config_updated,
                                                                                             bool runtime_apply_attempted) {
    result.events.push_back(event(DatabaseCredentialRotationState::Compensating,
                                  "compensating",
                                  "Compensating failed credential rotation"));

    auto manual_recovery = [&](const std::string& code, const std::string& message) {
        return fail_with(std::move(result), DatabaseCredentialRotationState::ManualRecoveryRequired, code, message);
    };

    result.events.push_back(event(DatabaseCredentialRotationState::Compensating,
                                  "restoring_mariadb_password",
                                  "Restoring MariaDB password"));
    auto step = dependencies_->restore_mariadb_password(request, new_password);
    if (!step.success) {
        return manual_recovery("manual_recovery_required", "Credential rotation requires manual recovery");
    }

    if (config_updated) {
        result.events.push_back(event(DatabaseCredentialRotationState::Compensating,
                                      "restoring_wordpress_config",
                                      "Restoring WordPress config"));
        step = dependencies_->restore_wordpress_config(request);
        if (!step.success) {
            return manual_recovery("manual_recovery_required", "Credential rotation requires manual recovery");
        }
    }

    if (config_updated || runtime_apply_attempted) {
        result.events.push_back(event(DatabaseCredentialRotationState::Compensating,
                                      "restoring_runtime",
                                      "Restoring runtime state"));
        step = dependencies_->restore_runtime(request);
        if (!step.success) {
            return manual_recovery("manual_recovery_required", "Credential rotation requires manual recovery");
        }
    }

    result.events.push_back(event(DatabaseCredentialRotationState::Compensating,
                                  "verifying_restored_credential",
                                  "Verifying restored database credential"));
    step = dependencies_->verify_old_credential(request);
    if (!step.success) {
        return manual_recovery("manual_recovery_required", "Credential rotation requires manual recovery");
    }

    result.success = false;
    result.final_state = DatabaseCredentialRotationState::Compensated;
    result.code = "rotation_compensated";
    result.message = "Credential rotation failed and was safely rolled back";
    result.events.push_back(event(result.final_state, result.code, result.message));
    return result;
}

DatabaseCredentialRotationResult DatabaseCredentialRotationService::rotate(const DatabaseCredentialRotationRequest& request) {
    DatabaseCredentialRotationResult result;
    if (request.site_id == 0) {
        return fail_with(std::move(result), DatabaseCredentialRotationState::Failed, "system_site_unsupported",
                         "System site credentials cannot be rotated");
    }
    if (request.database_id == 0) {
        return fail_with(std::move(result), DatabaseCredentialRotationState::Failed, "database_required",
                         "Database id is required");
    }

    OperationLock lock(*this, request.site_id, request.database_id);
    if (!lock.acquired()) {
        return fail_with(std::move(result), DatabaseCredentialRotationState::Failed, "rotation_already_running",
                         "A credential rotation is already running for this site database");
    }

    result.events.push_back(event(DatabaseCredentialRotationState::LockAcquired, "lock_acquired", "Rotation lock acquired"));
    if (dependencies_ == nullptr) {
        result.events.push_back(event(DatabaseCredentialRotationState::InspectingWordPress, "pending", "WordPress inspection dependency is not wired yet"));
        return fail_with(std::move(result), DatabaseCredentialRotationState::Failed, "rotation_dependencies_missing",
                         "Credential rotation dependencies are not wired yet");
    }

    auto run_step = [&](DatabaseCredentialRotationState state,
                        const std::string& code,
                        const std::string& message,
                        auto&& fn) -> DatabaseCredentialRotationStepResult {
        result.events.push_back(event(state, code, message));
        return fn();
    };

    auto step = run_step(DatabaseCredentialRotationState::InspectingWordPress,
                         "inspecting_wordpress",
                         "Inspecting WordPress credential source",
                         [&] { return dependencies_->inspect_wordpress(request); });
    if (!step.success) {
        return fail_with(std::move(result), DatabaseCredentialRotationState::Failed, step.code.empty() ? "wordpress_inspection_failed" : step.code,
                         "WordPress credential inspection failed");
    }

    step = run_step(DatabaseCredentialRotationState::VerifyingOldCredential,
                    "verifying_old_credential",
                    "Verifying existing database credential",
                    [&] { return dependencies_->verify_old_credential(request); });
    if (!step.success) {
        return fail_with(std::move(result), DatabaseCredentialRotationState::Failed, step.code.empty() ? "old_credential_verification_failed" : step.code,
                         "Existing database credential verification failed");
    }

    step = run_step(DatabaseCredentialRotationState::GeneratingPassword,
                    "generating_password",
                    "Generating replacement database credential",
                    [&] { return dependencies_->generate_password(request); });
    if (!step.success || step.generated_password.empty()) {
        return fail_with(std::move(result), DatabaseCredentialRotationState::Failed, step.code.empty() ? "password_generation_failed" : step.code,
                         "Replacement database credential generation failed");
    }
    const std::string new_password = step.generated_password;
    bool config_updated = false;
    bool runtime_apply_attempted = false;

    auto fail_after_mutation = [&](const std::string& code, const std::string& message) {
        result.events.push_back(event(DatabaseCredentialRotationState::Failed, code, message));
        return compensate_after_failure(std::move(result), request, new_password, config_updated, runtime_apply_attempted);
    };

    step = run_step(DatabaseCredentialRotationState::ChangingMariaDBPassword,
                    "changing_mariadb_password",
                    "Changing MariaDB password",
                    [&] { return dependencies_->change_mariadb_password(request, new_password); });
    if (!step.success) {
        return fail_with(std::move(result), DatabaseCredentialRotationState::Failed, step.code.empty() ? "mariadb_password_change_failed" : step.code,
                         "MariaDB password change failed");
    }

    step = run_step(DatabaseCredentialRotationState::UpdatingWordPressConfig,
                    "updating_wordpress_config",
                    "Updating WordPress config",
                    [&] { return dependencies_->update_wordpress_config(request, new_password); });
    if (!step.success) {
        return fail_after_mutation(step.code.empty() ? "wordpress_config_update_failed" : step.code,
                                   "WordPress config update failed");
    }
    config_updated = true;

    runtime_apply_attempted = true;
    step = run_step(DatabaseCredentialRotationState::ApplyingRuntime,
                    "applying_runtime",
                    "Applying runtime changes",
                    [&] { return dependencies_->apply_runtime(request); });
    if (!step.success) {
        return fail_after_mutation(step.code.empty() ? "runtime_apply_failed" : step.code,
                                   "Runtime apply failed");
    }

    step = run_step(DatabaseCredentialRotationState::VerifyingMariaDBPassword,
                    "verifying_mariadb_password",
                    "Verifying new MariaDB credential",
                    [&] { return dependencies_->verify_new_credential(request, new_password); });
    if (!step.success) {
        return fail_after_mutation(step.code.empty() ? "new_credential_verification_failed" : step.code,
                                   "New database credential verification failed");
    }

    step = run_step(DatabaseCredentialRotationState::VerifyingWordPress,
                    "verifying_wordpress",
                    "Verifying WordPress database access",
                    [&] { return dependencies_->verify_wordpress(request); });
    if (!step.success) {
        return fail_after_mutation(step.code.empty() ? "wordpress_verification_failed" : step.code,
                                   "WordPress verification failed");
    }

    step = run_step(DatabaseCredentialRotationState::VerifyingSiteHealth,
                    "verifying_site_health",
                    "Verifying site health",
                    [&] { return dependencies_->verify_site_health(request); });
    if (!step.success) {
        return fail_after_mutation(step.code.empty() ? "site_health_verification_failed" : step.code,
                                   "Site health verification failed");
    }

    step = run_step(DatabaseCredentialRotationState::PersistingMetadata,
                    "persisting_metadata",
                    "Persisting credential metadata",
                    [&] { return dependencies_->persist_metadata(request, new_password); });
    if (!step.success) {
        return fail_after_mutation(step.code.empty() ? "metadata_persist_failed" : step.code,
                                   "Credential metadata persistence failed");
    }

    result.success = true;
    result.final_state = DatabaseCredentialRotationState::Completed;
    result.code = "completed";
    result.message = "Database credential rotation completed";
    result.events.push_back(event(result.final_state, result.code, result.message));
    return result;
}

} // namespace containercp::database
