#include "database/DatabaseLifecycleService.h"

#include "database/DatabaseIdentifierValidator.h"
#include "database/DatabaseLifecycleAudit.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace containercp::database {
namespace {

std::vector<jobs::JobStep> lifecycle_steps() {
    const std::vector<std::string> names = {
        "Validating ownership",
        "Checking MariaDB runtime",
        "Preparing secure credentials",
        "Checking physical state",
        "Creating database",
        "Creating managed user",
        "Applying grants",
        "Verifying connection",
        "Persisting metadata",
        "Cleaning temporary credentials",
        "Compensating changes",
        "Completed",
        "Manual recovery required",
    };
    std::vector<jobs::JobStep> steps;
    for (const auto& name : names) {
        jobs::JobStep step;
        step.id = name;
        step.name = name;
        steps.push_back(std::move(step));
    }
    return steps;
}

void mark_step(std::vector<jobs::JobStep>& steps, const std::string& name, bool success, const std::string& code = {}) {
    for (auto& step : steps) {
        if (step.name == name) {
            step.started = true;
            step.skipped = false;
            step.completed = success;
            step.failed = !success;
            step.result = success ? "success" : "failure";
            step.error_code = code;
            return;
        }
    }
}

DatabaseLifecycleResult lifecycle_failure(std::vector<jobs::JobStep> steps,
                                          std::string step_name,
                                          std::string code,
                                          std::string message,
                                          bool manual_recovery = false) {
    mark_step(steps, step_name, false, code);
    DatabaseLifecycleResult result;
    result.success = false;
    result.code = std::move(code);
    result.message = std::move(message);
    result.steps = std::move(steps);
    result.failure.step = step_name;
    result.failure.step_name = step_name;
    result.failure.reason = result.message;
    result.failure.error_code = result.code;
    result.failure.manual_recovery_required = manual_recovery;
    result.manual_recovery_required = manual_recovery;
    return result;
}

DatabaseLifecycleResult lifecycle_success(std::vector<jobs::JobStep> steps, std::string code, std::string message) {
    mark_step(steps, "Completed", true);
    DatabaseLifecycleResult result;
    result.success = true;
    result.code = std::move(code);
    result.message = std::move(message);
    result.steps = std::move(steps);
    return result;
}

std::string env_value(const std::filesystem::path& path, const std::string& key) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const std::string prefix = key + "=";
        if (line.rfind(prefix, 0) == 0) {
            return line.substr(prefix.size());
        }
    }
    return {};
}

bool generic_confirmation(const std::string& value) {
    return value == "true" || value == "false" || value == "yes" || value == "no" || value == "delete" || value == "drop";
}

void audit_event(const std::string& operation,
                 const std::string& stage,
                 const std::string& result,
                 const std::string& code,
                 uint64_t job_id,
                 uint64_t site_id,
                 uint64_t database_id,
                 const std::string& domain,
                 bool manual_recovery = false,
                 DatabaseLifecycleAuditEvent::Level level = DatabaseLifecycleAuditEvent::Level::Info) {
    DatabaseLifecycleAuditLogger::log({operation, stage, result, code, job_id, site_id, database_id, domain, manual_recovery, level});
}

} // namespace

DatabaseLifecycleService::DatabaseLifecycleService(site::SiteManager& sites,
                                                   DatabaseManager& databases,
                                                   runtime::SiteRuntimeManager& site_runtime,
                                                   const DatabaseProvider& provider,
                                                   std::filesystem::path sites_root,
                                                   PersistCallback persist)
    : sites_(sites)
    , databases_(databases)
    , site_runtime_(&site_runtime)
    , runtime_lookup_([&site_runtime](const site::Site& site_record) {
        return site_runtime.get_status(site_record.id, site_record.domain).db.status;
    })
    , provider_(provider)
    , sites_root_(std::move(sites_root))
    , persist_(std::move(persist)) {
}

DatabaseLifecycleService::DatabaseLifecycleService(site::SiteManager& sites,
                                                   DatabaseManager& databases,
                                                   RuntimeDbStatusLookup runtime_lookup,
                                                   const DatabaseProvider& provider,
                                                   std::filesystem::path sites_root,
                                                   PersistCallback persist)
    : sites_(sites)
    , databases_(databases)
    , site_runtime_(nullptr)
    , runtime_lookup_(std::move(runtime_lookup))
    , provider_(provider)
    , sites_root_(std::move(sites_root))
    , persist_(std::move(persist)) {
}

bool database_drop_confirmation_valid(const std::string& confirmation,
                                      const std::string& database_name,
                                      const std::string& domain) {
    if (confirmation.empty() || confirmation.size() > 253 || generic_confirmation(confirmation)) {
        return false;
    }
    for (unsigned char c : confirmation) {
        if (std::iscntrl(c) != 0) {
            return false;
        }
    }
    return confirmation == database_name || confirmation == domain;
}

bool DatabaseLifecycleService::site_has_exactly_one_database(uint64_t site_id) const {
    int count = 0;
    for (const auto& database : databases_.list()) {
        if (database.site_id == site_id && database.enabled) {
            ++count;
        }
    }
    return count == 1;
}

MariaDBConnectionTarget DatabaseLifecycleService::target_for_site(const site::Site& site_record) const {
    return {(sites_root_ / site_record.domain / "docker-compose.yml").string(), "mariadb"};
}

DatabaseProviderCredential DatabaseLifecycleService::load_service_account(const site::Site& site_record) const {
    const auto env_path = sites_root_ / site_record.domain / ".env";
    DatabaseProviderCredential credential;
    credential.user = env_value(env_path, "CONTAINERCP_DB_SERVICE_USER");
    credential.password = env_value(env_path, "CONTAINERCP_DB_SERVICE_PASSWORD");
    credential.host = "localhost";
    return credential;
}

DatabaseLifecycleResult DatabaseLifecycleService::resolve_target(uint64_t site_id,
                                                                 uint64_t database_id,
                                                                 bool require_managed,
                                                                 ResolvedTarget& resolved,
                                                                 const std::string& operation,
                                                                 uint64_t job_id) const {
    auto steps = lifecycle_steps();
    auto* site_record = sites_.find_by_id(site_id);
    if (site_record == nullptr) {
        audit_event(operation, "validate", "failure", "site_not_found", job_id, site_id, database_id, "", false, DatabaseLifecycleAuditEvent::Level::Error);
        return lifecycle_failure(std::move(steps), "Validating ownership", "site_not_found", "Site was not found");
    }
    auto* database = databases_.find(database_id);
    if (database == nullptr || database->site_id != site_id) {
        audit_event(operation, "validate", "failure", "database_not_found", job_id, site_id, database_id, site_record->domain, false, DatabaseLifecycleAuditEvent::Level::Error);
        return lifecycle_failure(std::move(steps), "Validating ownership", "database_not_found", "Database was not found for this site");
    }
    if (!site_has_exactly_one_database(site_id)) {
        return lifecycle_failure(std::move(steps), "Validating ownership", "database_cardinality_invalid", "Site must have exactly one managed application database");
    }
    if (require_managed && database->db_password.empty()) {
        return lifecycle_failure(std::move(steps), "Validating ownership", "ownership_not_managed", "Imported or ownership-uncertain databases cannot be mutated");
    }
    if (!DatabaseIdentifierValidator::validate_database_name(database->db_name).valid ||
        !DatabaseIdentifierValidator::validate_user_name(database->db_user).valid) {
        return lifecycle_failure(std::move(steps), "Validating ownership", "identifier_invalid", "Database metadata contains unsupported identifiers");
    }
    mark_step(steps, "Validating ownership", true);
    resolved.site_record = site_record;
    resolved.database = database;
    resolved.target = target_for_site(*site_record);
    resolved.service_account = load_service_account(*site_record);
    if (resolved.service_account.user.empty() || resolved.service_account.password.empty()) {
        return lifecycle_failure(std::move(steps), "Preparing secure credentials", "service_account_unavailable", "ContainerCP MariaDB service account is unavailable for this site");
    }
    mark_step(steps, "Preparing secure credentials", true);
    DatabaseLifecycleResult ok;
    ok.success = true;
    ok.steps = std::move(steps);
    return ok;
}

DatabaseLifecycleResult DatabaseLifecycleService::createManagedDatabase(const DatabaseCreateManagedRequest& request) {
    ResolvedTarget resolved;
    auto resolved_result = resolve_target(request.site_id, request.database_id, true, resolved, "create", request.job_id);
    auto steps = std::move(resolved_result.steps);
    if (!resolved_result.success) return resolved_result;

    const auto runtime = runtime_lookup_(*resolved.site_record);
    if (runtime != "Running") {
        return lifecycle_failure(std::move(steps), "Checking MariaDB runtime", "runtime_unavailable", "MariaDB runtime is not running");
    }
    mark_step(steps, "Checking MariaDB runtime", true);

    auto service_check = provider_.verify_service_account(resolved.target, resolved.service_account);
    if (!service_check.success) {
        return lifecycle_failure(std::move(steps), "Preparing secure credentials", service_check.code, "MariaDB service account verification failed");
    }

    const auto db_exists = provider_.database_exists(resolved.target, resolved.service_account, resolved.database->db_name);
    const auto user_exists = provider_.user_exists(resolved.target, resolved.service_account, resolved.database->db_user);
    if (db_exists.success || user_exists.success) {
        return lifecycle_failure(std::move(steps), "Checking physical state", "physical_conflict", "Target database or user already exists");
    }
    mark_step(steps, "Checking physical state", true);

    bool created_database = false;
    bool created_user = false;
    bool granted = false;
    auto compensate = [&]() {
        mark_step(steps, "Compensating changes", true);
        if (granted) (void)provider_.revoke_database_privileges(resolved.target, resolved.service_account, resolved.database->db_name, resolved.database->db_user);
        if (created_user) (void)provider_.drop_user(resolved.target, resolved.service_account, resolved.database->db_user);
        if (created_database) (void)provider_.drop_database(resolved.target, resolved.service_account, resolved.database->db_name);
        databases_.remove(resolved.database->id);
        (void)persist_();
    };

    auto create_db = provider_.create_database(resolved.target, resolved.service_account, resolved.database->db_name);
    if (!create_db.success) {
        compensate();
        return lifecycle_failure(std::move(steps), "Creating database", create_db.code, "Physical database creation failed");
    }
    created_database = true;
    mark_step(steps, "Creating database", true);

    auto create_user = provider_.create_or_update_user(resolved.target, resolved.service_account, resolved.database->db_user, resolved.database->db_password);
    if (!create_user.success) {
        compensate();
        return lifecycle_failure(std::move(steps), "Creating managed user", create_user.code, "Managed user creation failed");
    }
    created_user = true;
    mark_step(steps, "Creating managed user", true);

    auto grant = provider_.grant_database_privileges(resolved.target, resolved.service_account, resolved.database->db_name, resolved.database->db_user);
    if (!grant.success) {
        compensate();
        return lifecycle_failure(std::move(steps), "Applying grants", grant.code, "Database privilege grant failed");
    }
    granted = true;
    mark_step(steps, "Applying grants", true);

    auto login = provider_.verify_login(resolved.target, resolved.database->db_name, resolved.database->db_user, resolved.database->db_password);
    if (!login.success) {
        compensate();
        return lifecycle_failure(std::move(steps), "Verifying connection", login.code, "Managed database login verification failed");
    }
    mark_step(steps, "Verifying connection", true);

    if (!persist_()) {
        compensate();
        return lifecycle_failure(std::move(steps), "Persisting metadata", "metadata_persist_failed", "Database metadata persistence failed");
    }
    mark_step(steps, "Persisting metadata", true);
    mark_step(steps, "Cleaning temporary credentials", true);
    audit_event("create", "completed", "success", {}, request.job_id, request.site_id, request.database_id, resolved.site_record->domain);
    return lifecycle_success(std::move(steps), "created", "Managed database created");
}

DatabaseLifecycleResult DatabaseLifecycleService::verifyManagedDatabase(const DatabaseVerifyManagedRequest& request) {
    ResolvedTarget resolved;
    auto resolved_result = resolve_target(request.site_id, request.database_id, false, resolved, "verify", request.job_id);
    auto steps = std::move(resolved_result.steps);
    if (!resolved_result.success) return resolved_result;
    const auto runtime = runtime_lookup_(*resolved.site_record);
    if (runtime != "Running") {
        return lifecycle_failure(std::move(steps), "Checking MariaDB runtime", "runtime_unavailable", "MariaDB runtime is not running");
    }
    mark_step(steps, "Checking MariaDB runtime", true);
    auto service_check = provider_.verify_service_account(resolved.target, resolved.service_account);
    if (!service_check.success) {
        return lifecycle_failure(std::move(steps), "Preparing secure credentials", service_check.code, "MariaDB service account verification failed");
    }
    auto db_exists = provider_.database_exists(resolved.target, resolved.service_account, resolved.database->db_name);
    auto user_exists = provider_.user_exists(resolved.target, resolved.service_account, resolved.database->db_user);
    if (!db_exists.success || !user_exists.success) {
        return lifecycle_failure(std::move(steps), "Checking physical state", "physical_state_missing", "Database or managed user is missing");
    }
    mark_step(steps, "Checking physical state", true);
    auto login = provider_.verify_login(resolved.target, resolved.database->db_name, resolved.database->db_user, resolved.database->db_password);
    if (!login.success) {
        return lifecycle_failure(std::move(steps), "Verifying connection", login.code, "Managed database login verification failed");
    }
    mark_step(steps, "Verifying connection", true);
    mark_step(steps, "Cleaning temporary credentials", true);
    audit_event("verify", "completed", "success", {}, request.job_id, request.site_id, request.database_id, resolved.site_record->domain);
    return lifecycle_success(std::move(steps), "verified", "Managed database verified");
}

DatabaseLifecycleResult DatabaseLifecycleService::dropManagedDatabase(const DatabaseDropManagedRequest& request) {
    ResolvedTarget resolved;
    auto resolved_result = resolve_target(request.site_id, request.database_id, true, resolved, "drop", request.job_id);
    auto steps = std::move(resolved_result.steps);
    if (!resolved_result.success) return resolved_result;
    if (resolved.site_record->domain != request.expected_domain || resolved.database->db_name != request.expected_database_name) {
        return lifecycle_failure(std::move(steps), "Validating ownership", "target_relation_changed", "Database target changed after the drop request was created");
    }
    if (!database_drop_confirmation_valid(request.confirmation, resolved.database->db_name, resolved.site_record->domain)) {
        audit_event("drop", "confirmation", "rejected", "confirmation_mismatch", request.job_id, request.site_id, request.database_id, resolved.site_record->domain, false, DatabaseLifecycleAuditEvent::Level::Warning);
        return lifecycle_failure(std::move(steps), "Validating ownership", "confirmation_mismatch", "Confirmation must match the database name or site domain");
    }
    audit_event("drop", "confirmation", "accepted", {}, request.job_id, request.site_id, request.database_id, resolved.site_record->domain);

    const auto runtime = runtime_lookup_(*resolved.site_record);
    if (runtime != "Running") {
        return lifecycle_failure(std::move(steps), "Checking MariaDB runtime", "runtime_unavailable", "MariaDB runtime is not running");
    }
    mark_step(steps, "Checking MariaDB runtime", true);

    auto service_check = provider_.verify_service_account(resolved.target, resolved.service_account);
    if (!service_check.success) {
        return lifecycle_failure(std::move(steps), "Preparing secure credentials", service_check.code, "MariaDB service account verification failed");
    }

    auto db_exists = provider_.database_exists(resolved.target, resolved.service_account, resolved.database->db_name);
    auto user_exists = provider_.user_exists(resolved.target, resolved.service_account, resolved.database->db_user);
    mark_step(steps, "Checking physical state", true);
    if (db_exists.success) {
        (void)provider_.revoke_database_privileges(resolved.target, resolved.service_account, resolved.database->db_name, resolved.database->db_user);
        auto drop_db = provider_.drop_database(resolved.target, resolved.service_account, resolved.database->db_name);
        if (!drop_db.success) {
            return lifecycle_failure(std::move(steps), "Compensating changes", drop_db.code, "Physical database drop failed", true);
        }
    }
    if (user_exists.success) {
        auto grants = provider_.user_schema_grant_count(resolved.target, resolved.service_account, resolved.database->db_user);
        if (grants.success && (grants.output.empty() || grants.output[0] == '0' || grants.output[0] == '1')) {
            (void)provider_.drop_user(resolved.target, resolved.service_account, resolved.database->db_user);
        }
    }
    mark_step(steps, "Compensating changes", true);
    databases_.remove(resolved.database->id);
    if (!persist_()) {
        return lifecycle_failure(std::move(steps), "Persisting metadata", "metadata_persist_failed", "Metadata removal failed after physical drop", true);
    }
    mark_step(steps, "Persisting metadata", true);
    mark_step(steps, "Cleaning temporary credentials", true);
    audit_event("drop", "completed", "success", {}, request.job_id, request.site_id, request.database_id, resolved.site_record->domain);
    return lifecycle_success(std::move(steps), db_exists.success ? "dropped" : "already_absent_metadata_removed", "Managed database dropped");
}

bool DatabaseLifecycleService::can_drop(const Database& database) const {
    return database.enabled && !database.db_password.empty() && site_has_exactly_one_database(database.site_id);
}

std::string DatabaseLifecycleService::drop_block_reason(const Database& database) const {
    if (!database.enabled) return "database_disabled";
    if (database.db_password.empty()) return "ownership_not_managed";
    if (!site_has_exactly_one_database(database.site_id)) return "database_cardinality_invalid";
    return {};
}

} // namespace containercp::database
