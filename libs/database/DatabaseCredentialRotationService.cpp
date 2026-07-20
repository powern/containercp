#include "DatabaseCredentialRotationService.h"

#include "logger/Logger.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <utility>

namespace containercp::database {
namespace {

struct RotationStepSpec {
    std::string_view id;
    std::string_view name;
};

const std::vector<RotationStepSpec>& rotation_step_specs() {
    static const std::vector<RotationStepSpec> specs = {
        {"acquire_lock", "Acquire operation lock"},
        {"load_metadata", "Load metadata"},
        {"inspect_wordpress", "Inspect WordPress"},
        {"resolve_database_target", "Resolve database target"},
        {"verify_shared_credentials", "Verify shared credentials"},
        {"load_mariadb_admin_credentials", "Load MariaDB admin credentials"},
        {"verify_current_password", "Verify current password"},
        {"generate_new_password", "Generate new password"},
        {"update_mariadb_password", "Update MariaDB password"},
        {"update_wp_config", "Update wp-config.php"},
        {"persist_metadata", "Persist metadata"},
        {"runtime_verification", "Runtime verification"},
        {"commit", "Commit"},
        {"compensation", "Compensation"},
        {"manual_recovery_required", "ManualRecoveryRequired"},
    };
    return specs;
}

std::string utc_now() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ts;
    ts << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
    return ts.str();
}

std::vector<jobs::JobStep> initial_rotation_steps() {
    std::vector<jobs::JobStep> steps;
    for (const auto& spec : rotation_step_specs()) {
        jobs::JobStep step;
        step.id = std::string(spec.id);
        step.name = std::string(spec.name);
        steps.push_back(std::move(step));
    }
    return steps;
}

jobs::JobStep* find_step(std::vector<jobs::JobStep>& steps, std::string_view id) {
    for (auto& step : steps) {
        if (step.id == id) {
            return &step;
        }
    }
    return nullptr;
}

const jobs::JobStep* find_failed_step(const std::vector<jobs::JobStep>& steps) {
    for (const auto& step : steps) {
        if (step.failed) {
            return &step;
        }
    }
    return nullptr;
}

void log_rotation_stage(const DatabaseCredentialRotationRequest& request,
                        std::string_view event,
                        std::string_view step_id,
                        const std::string& code = {}) {
    std::string message = "credential_rotation event=" + std::string(event) +
                          " job_id=" + std::to_string(request.job_id) +
                          " site_id=" + std::to_string(request.site_id) +
                          " database_id=" + std::to_string(request.database_id) +
                          " step=" + std::string(step_id);
    if (!code.empty()) {
        message += " code=" + code;
    }
    if (event == "FAILURE") {
        logger::Logger::instance().error("AUDIT", message);
    } else if (event == "COMPENSATION") {
        logger::Logger::instance().warning("AUDIT", message);
    } else {
        logger::Logger::instance().info("AUDIT", message);
    }
}

DatabaseCredentialRotationEvent event(DatabaseCredentialRotationState state, std::string code, std::string message) {
    return {state, std::move(code), std::move(message)};
}

void complete_step(std::vector<jobs::JobStep>& steps,
                   std::string_view id,
                   const std::chrono::steady_clock::time_point& started,
                   bool success,
                   const std::string& result,
                   const std::string& message,
                   const std::string& error_code = {}) {
    auto* step = find_step(steps, id);
    if (step == nullptr) {
        return;
    }
    step->started = true;
    step->completed = success;
    step->failed = !success;
    step->skipped = false;
    step->duration_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::steady_clock::now() - started).count());
    step->result = result;
    step->message = message;
    step->error_code = error_code;
    step->completed_at = utc_now();
}

void start_step(std::vector<jobs::JobStep>& steps, std::string_view id) {
    auto* step = find_step(steps, id);
    if (step == nullptr) {
        return;
    }
    step->started = true;
    step->skipped = false;
    step->started_at = utc_now();
}

void update_failure_diagnostics(DatabaseCredentialRotationResult& result) {
    if (const auto* failed = find_failed_step(result.steps)) {
        result.failure.step = failed->id;
        result.failure.step_name = failed->name;
        result.failure.reason = failed->message;
        result.failure.error_code = failed->error_code;
    } else {
        result.failure.reason = result.message;
        result.failure.error_code = result.code;
    }
    result.failure.compensation_started = false;
    result.failure.compensation_result = "not_started";
    result.failure.manual_recovery_required = result.final_state == DatabaseCredentialRotationState::ManualRecoveryRequired;
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
    update_failure_diagnostics(result);
    return result;
}

DatabaseCredentialRotationResult manual_recovery(DatabaseCredentialRotationResult result) {
    return fail_with(std::move(result), DatabaseCredentialRotationState::ManualRecoveryRequired,
                     "manual_recovery_required", "Credential rotation requires manual recovery");
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
    case DatabaseCredentialRotationState::AssessingSharedUser:
        return "assessing_shared_user";
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
        return "verifying_runtime_availability";
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
    const auto compensation_started = std::chrono::steady_clock::now();
    start_step(result.steps, "compensation");
    log_rotation_stage(request, "COMPENSATION", "compensation", "started");
    result.events.push_back(event(DatabaseCredentialRotationState::Compensating,
                                   "compensating",
                                   "Compensating failed credential rotation"));

    auto manual_recovery = [&](const std::string& code, const std::string& message) {
        complete_step(result.steps, "compensation", compensation_started, false, "failed",
                      "Compensation failed", code);
        const auto manual_started = std::chrono::steady_clock::now();
        start_step(result.steps, "manual_recovery_required");
        complete_step(result.steps, "manual_recovery_required", manual_started, true, "entered",
                      "Manual recovery is required", "manual_recovery_required");
        auto failed = fail_with(std::move(result), DatabaseCredentialRotationState::ManualRecoveryRequired, code, message);
        failed.failure.compensation_started = true;
        failed.failure.compensation_result = "failed";
        failed.failure.manual_recovery_required = true;
        log_rotation_stage(request, "FAILURE", "compensation", code);
        log_rotation_stage(request, "FAILURE", "manual_recovery_required", "manual_recovery_required");
        return failed;
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

    result.events.push_back(event(DatabaseCredentialRotationState::Compensating,
                                  "verifying_restored_wordpress",
                                  "Verifying restored WordPress database access"));
    step = dependencies_->verify_restored_wordpress(request);
    if (!step.success) {
        return manual_recovery("manual_recovery_required", "Credential rotation requires manual recovery");
    }

    result.events.push_back(event(DatabaseCredentialRotationState::Compensating,
                                  "verifying_restored_runtime_availability",
                                  "Verifying restored runtime container availability"));
    step = dependencies_->verify_restored_site_health(request);
    if (!step.success) {
        return manual_recovery("manual_recovery_required", "Credential rotation requires manual recovery");
    }

    result.events.push_back(event(DatabaseCredentialRotationState::Compensating,
                                  "verifying_restored_metadata",
                                  "Verifying restored credential metadata"));
    step = dependencies_->verify_restored_metadata(request);
    if (!step.success) {
        return manual_recovery("manual_recovery_required", "Credential rotation requires manual recovery");
    }

    result.success = false;
    result.final_state = DatabaseCredentialRotationState::Compensated;
    result.code = "rotation_compensated";
    result.message = "Credential rotation failed and was safely rolled back";
    complete_step(result.steps, "compensation", compensation_started, true, "completed",
                  "Compensation completed successfully");
    result.events.push_back(event(result.final_state, result.code, result.message));
    update_failure_diagnostics(result);
    result.failure.compensation_started = true;
    result.failure.compensation_result = "completed";
    result.failure.manual_recovery_required = false;
    log_rotation_stage(request, "SUCCESS", "compensation", "rotation_compensated");
    return result;
}

DatabaseCredentialRotationResult DatabaseCredentialRotationService::rotate(const DatabaseCredentialRotationRequest& request) {
    DatabaseCredentialRotationResult result;
    result.steps = initial_rotation_steps();
    if (request.database_id == 0) {
        const auto started = std::chrono::steady_clock::now();
        start_step(result.steps, "load_metadata");
        complete_step(result.steps, "load_metadata", started, false, "failed",
                      "Database id is required", "database_required");
        log_rotation_stage(request, "FAILURE", "load_metadata", "database_required");
        return fail_with(std::move(result), DatabaseCredentialRotationState::Failed, "database_required",
                          "Database id is required");
    }

    auto lock_started = std::chrono::steady_clock::now();
    start_step(result.steps, "acquire_lock");
    log_rotation_stage(request, "START", "acquire_lock");
    OperationLock lock(*this, request.site_id, request.database_id);
    if (!lock.acquired()) {
        complete_step(result.steps, "acquire_lock", lock_started, false, "failed",
                      "A credential rotation is already running for this site database", "rotation_already_running");
        log_rotation_stage(request, "FAILURE", "acquire_lock", "rotation_already_running");
        return fail_with(std::move(result), DatabaseCredentialRotationState::Failed, "rotation_already_running",
                          "A credential rotation is already running for this site database");
    }

    complete_step(result.steps, "acquire_lock", lock_started, true, "success", "Rotation lock acquired");
    log_rotation_stage(request, "SUCCESS", "acquire_lock");
    result.events.push_back(event(DatabaseCredentialRotationState::LockAcquired, "lock_acquired", "Rotation lock acquired"));
    if (dependencies_ == nullptr) {
        const auto started = std::chrono::steady_clock::now();
        start_step(result.steps, "load_metadata");
        complete_step(result.steps, "load_metadata", started, false, "failed",
                      "Credential rotation dependencies are not wired yet", "rotation_dependencies_missing");
        log_rotation_stage(request, "FAILURE", "load_metadata", "rotation_dependencies_missing");
        result.events.push_back(event(DatabaseCredentialRotationState::InspectingWordPress, "pending", "WordPress inspection dependency is not wired yet"));
        return fail_with(std::move(result), DatabaseCredentialRotationState::Failed, "rotation_dependencies_missing",
                          "Credential rotation dependencies are not wired yet");
    }

    auto run_step = [&](std::string_view step_id,
                        DatabaseCredentialRotationState state,
                        const std::string& start_code,
                        const std::string& start_message,
                        auto&& fn) -> DatabaseCredentialRotationStepResult {
        const auto started = std::chrono::steady_clock::now();
        start_step(result.steps, step_id);
        log_rotation_stage(request, "START", step_id);
        result.events.push_back(event(state, start_code, start_message));
        auto step = fn();
        if (step.success) {
            complete_step(result.steps, step_id, started, true, step.code.empty() ? "success" : step.code,
                          step.message.empty() ? start_message : step.message);
            log_rotation_stage(request, "SUCCESS", step_id, step.code);
        }
        return step;
    };

    auto mark_failed_step = [&](std::string_view step_id,
                                const DatabaseCredentialRotationStepResult& step,
                                const std::string& fallback_code,
                                const std::string& safe_message) {
        auto* detail = find_step(result.steps, step_id);
        if (detail != nullptr && !detail->failed) {
            detail->completed = false;
            detail->failed = true;
            detail->skipped = false;
            detail->result = "failed";
            detail->message = safe_message;
            detail->error_code = step.code.empty() ? fallback_code : step.code;
            if (detail->completed_at.empty()) {
                detail->completed_at = utc_now();
            }
        }
        log_rotation_stage(request, "FAILURE", step_id, step.code.empty() ? fallback_code : step.code);
    };

    auto fail_step = [&](std::string_view step_id,
                         DatabaseCredentialRotationState state,
                         const DatabaseCredentialRotationStepResult& step,
                         const std::string& fallback_code,
                         const std::string& safe_message) {
        mark_failed_step(step_id, step, fallback_code, safe_message);
        return fail_with(std::move(result), state, step.code.empty() ? fallback_code : step.code, safe_message);
    };

    auto step = run_step("load_metadata",
                         DatabaseCredentialRotationState::InspectingWordPress,
                         "loading_metadata",
                         "Loading credential metadata",
                         [&] { return dependencies_->load_metadata(request); });
    if (!step.success) {
        return fail_step("load_metadata", DatabaseCredentialRotationState::Failed, step,
                         "metadata_load_failed", "Credential metadata loading failed");
    }

    step = run_step("inspect_wordpress",
                    DatabaseCredentialRotationState::InspectingWordPress,
                          "inspecting_wordpress",
                          "Inspecting WordPress credential source",
                          [&] { return dependencies_->inspect_wordpress(request); });
    if (!step.success) {
        return fail_step("inspect_wordpress", DatabaseCredentialRotationState::Failed, step,
                         "wordpress_inspection_failed", "WordPress credential inspection failed");
    }

    step = run_step("resolve_database_target",
                    DatabaseCredentialRotationState::InspectingWordPress,
                    "resolving_database_target",
                    "Resolving WordPress database target",
                    [&] { return dependencies_->resolve_database_target(request); });
    if (!step.success) {
        return fail_step("resolve_database_target", DatabaseCredentialRotationState::Failed, step,
                         "database_target_resolution_failed", "WordPress database target resolution failed");
    }

    step = run_step("load_mariadb_admin_credentials",
                    DatabaseCredentialRotationState::VerifyingOldCredential,
                    "loading_mariadb_admin_credentials",
                    "Loading MariaDB admin credentials",
                    [&] { return dependencies_->load_mariadb_admin_credentials(request); });
    if (!step.success) {
        return fail_step("load_mariadb_admin_credentials", DatabaseCredentialRotationState::Failed, step,
                         "mariadb_admin_credentials_unavailable", "MariaDB admin credentials are unavailable");
    }

    step = run_step("verify_shared_credentials",
                    DatabaseCredentialRotationState::AssessingSharedUser,
                    "assessing_shared_user",
                    "Assessing shared database credential risk",
                    [&] { return dependencies_->assess_shared_user(request); });
    if (!step.success) {
        return fail_step("verify_shared_credentials", DatabaseCredentialRotationState::Failed, step,
                         "shared_user_assessment_failed", "Shared credential assessment failed");
    }
    if (!mariadb_shared_credential_rotation_allowed(step.shared_assessment.state)) {
        const auto state = mariadb_shared_credential_assessment_state_to_string(step.shared_assessment.state);
        DatabaseCredentialRotationStepResult blocked;
        blocked.success = false;
        blocked.code = state == "unknown" ? "shared_user_assessment_unknown" : "shared_user_assessment_" + state;
        return fail_step("verify_shared_credentials", DatabaseCredentialRotationState::Failed, blocked,
                         blocked.code, "Shared credential assessment blocks rotation");
    }

    step = run_step("verify_current_password",
                    DatabaseCredentialRotationState::VerifyingOldCredential,
                    "verifying_old_credential",
                    "Verifying existing database credential",
                    [&] { return dependencies_->verify_old_credential(request); });
    if (!step.success) {
        return fail_step("verify_current_password", DatabaseCredentialRotationState::Failed, step,
                         "old_credential_verification_failed", "Existing database credential verification failed");
    }

    step = run_step("generate_new_password",
                    DatabaseCredentialRotationState::GeneratingPassword,
                    "generating_password",
                    "Generating replacement database credential",
                    [&] { return dependencies_->generate_password(request); });
    if (!step.success || step.generated_password.empty()) {
        return fail_step("generate_new_password", DatabaseCredentialRotationState::Failed, step,
                         "password_generation_failed", "Replacement database credential generation failed");
    }
    const std::string new_password = step.generated_password;
    bool config_updated = false;
    bool runtime_apply_attempted = false;

    auto fail_after_mutation = [&](const std::string& code, const std::string& message) {
        result.events.push_back(event(DatabaseCredentialRotationState::Failed, code, message));
        return compensate_after_failure(std::move(result), request, new_password, config_updated, runtime_apply_attempted);
    };

    step = run_step("update_mariadb_password",
                    DatabaseCredentialRotationState::ChangingMariaDBPassword,
                    "changing_mariadb_password",
                    "Changing MariaDB password",
                    [&] { return dependencies_->change_mariadb_password(request, new_password); });
    if (!step.success) {
        const std::string change_failure_code = step.code.empty() ? "mariadb_password_change_failed" : step.code;
        auto* detail = find_step(result.steps, "update_mariadb_password");
        if (detail != nullptr) {
            detail->failed = true;
            detail->completed = false;
            detail->result = "failed";
            detail->message = "MariaDB password change failed";
            detail->error_code = change_failure_code;
            detail->completed_at = utc_now();
        }
        log_rotation_stage(request, "FAILURE", "update_mariadb_password", change_failure_code);
        result.events.push_back(event(DatabaseCredentialRotationState::VerifyingMariaDBPassword,
                                       "determining_mariadb_password_state",
                                      "Determining MariaDB password state after failed change command"));
        const auto old_check = dependencies_->probe_old_credential(request);
        const auto new_check = dependencies_->probe_new_credential(request, new_password);
        if (new_check.success && !old_check.success) {
            if (detail != nullptr) {
                detail->failed = false;
                detail->completed = true;
                detail->result = "confirmed_after_command_failure";
                detail->message = "MariaDB password change confirmed after command failure";
                detail->error_code.clear();
            }
            result.events.push_back(event(DatabaseCredentialRotationState::ChangingMariaDBPassword,
                                           "mariadb_password_change_confirmed",
                                           "MariaDB password change confirmed after command failure"));
        } else if (old_check.success && !new_check.success) {
            return fail_step("update_mariadb_password", DatabaseCredentialRotationState::Failed, step,
                             change_failure_code, "MariaDB password change failed");
        } else {
            if (detail != nullptr) {
                detail->message = "MariaDB password state could not be proven after change command failure";
                detail->error_code = old_check.success && new_check.success
                                         ? "mariadb_password_state_ambiguous"
                                         : "mariadb_password_state_unknown";
            }
            result.events.push_back(event(DatabaseCredentialRotationState::ManualRecoveryRequired,
                                           old_check.success && new_check.success
                                               ? "mariadb_password_state_ambiguous"
                                               : "mariadb_password_state_unknown",
                                          "MariaDB password state could not be proven after change command failure"));
            const auto manual_started = std::chrono::steady_clock::now();
            start_step(result.steps, "manual_recovery_required");
            complete_step(result.steps, "manual_recovery_required", manual_started, true, "entered",
                          "Manual recovery is required", "manual_recovery_required");
            log_rotation_stage(request, "FAILURE", "manual_recovery_required", "manual_recovery_required");
            auto failed = manual_recovery(std::move(result));
            failed.failure.manual_recovery_required = true;
            return failed;
        }
    }

    step = run_step("update_wp_config",
                    DatabaseCredentialRotationState::UpdatingWordPressConfig,
                    "updating_wordpress_config",
                    "Updating WordPress config",
                    [&] { return dependencies_->update_wordpress_config(request, new_password); });
    if (!step.success) {
        mark_failed_step("update_wp_config", step, "wordpress_config_update_failed", "WordPress config update failed");
        return fail_after_mutation(step.code.empty() ? "wordpress_config_update_failed" : step.code,
                                    "WordPress config update failed");
    }
    config_updated = true;

    runtime_apply_attempted = true;
    step = run_step("runtime_verification",
                    DatabaseCredentialRotationState::ApplyingRuntime,
                    "applying_runtime",
                    "Applying runtime changes",
                    [&] { return dependencies_->apply_runtime(request); });
    if (!step.success) {
        mark_failed_step("runtime_verification", step, "runtime_apply_failed", "Runtime apply failed");
        return fail_after_mutation(step.code.empty() ? "runtime_apply_failed" : step.code,
                                    "Runtime apply failed");
    }

    step = run_step("runtime_verification",
                    DatabaseCredentialRotationState::VerifyingMariaDBPassword,
                    "verifying_mariadb_password",
                    "Verifying new MariaDB credential",
                    [&] { return dependencies_->verify_new_credential(request, new_password); });
    if (!step.success) {
        mark_failed_step("runtime_verification", step, "new_credential_verification_failed", "New database credential verification failed");
        return fail_after_mutation(step.code.empty() ? "new_credential_verification_failed" : step.code,
                                    "New database credential verification failed");
    }

    step = run_step("runtime_verification",
                    DatabaseCredentialRotationState::VerifyingWordPress,
                    "verifying_wordpress",
                    "Verifying WordPress database access",
                    [&] { return dependencies_->verify_wordpress(request); });
    if (!step.success) {
        mark_failed_step("runtime_verification", step, "wordpress_verification_failed", "WordPress verification failed");
        return fail_after_mutation(step.code.empty() ? "wordpress_verification_failed" : step.code,
                                    "WordPress verification failed");
    }

    step = run_step("runtime_verification",
                    DatabaseCredentialRotationState::VerifyingSiteHealth,
                    "verifying_runtime_availability",
                    "Verifying runtime container availability",
                    [&] { return dependencies_->verify_site_health(request); });
    if (!step.success) {
        mark_failed_step("runtime_verification", step, "runtime_availability_verification_failed", "Runtime container availability verification failed");
        return fail_after_mutation(step.code.empty() ? "runtime_availability_verification_failed" : step.code,
                                    "Runtime container availability verification failed");
    }

    step = run_step("persist_metadata",
                    DatabaseCredentialRotationState::PersistingMetadata,
                    "persisting_metadata",
                    "Persisting credential metadata",
                    [&] { return dependencies_->persist_metadata(request, new_password); });
    if (!step.success) {
        mark_failed_step("persist_metadata", step, "metadata_persist_failed", "Credential metadata persistence failed");
        return fail_after_mutation(step.code.empty() ? "metadata_persist_failed" : step.code,
                                    "Credential metadata persistence failed");
    }

    const auto commit_started = std::chrono::steady_clock::now();
    start_step(result.steps, "commit");
    log_rotation_stage(request, "START", "commit");
    result.success = true;
    result.final_state = DatabaseCredentialRotationState::Completed;
    result.code = "completed";
    result.message = "Database credential rotation completed";
    complete_step(result.steps, "commit", commit_started, true, "success", "Database credential rotation committed");
    log_rotation_stage(request, "SUCCESS", "commit", "completed");
    result.events.push_back(event(result.final_state, result.code, result.message));
    return result;
}

} // namespace containercp::database
