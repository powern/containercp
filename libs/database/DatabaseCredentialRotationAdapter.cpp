#include "database/DatabaseCredentialRotationAdapter.h"

#include "wordpress/WordPressConfigTypes.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace containercp::database {
namespace {

std::string trim_cr(std::string value) {
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
    return value;
}

std::string env_value(const std::filesystem::path& env_path, const std::string& key) {
    std::ifstream in(env_path);
    if (!in.is_open()) {
        return {};
    }
    std::string line;
    const std::string prefix = key + "=";
    while (std::getline(in, line)) {
        line = trim_cr(line);
        if (line.rfind(prefix, 0) == 0) {
            return line.substr(prefix.size());
        }
    }
    return {};
}

bool wordpress_direct_complete(const wordpress::WordPressConfigServiceResult& result) {
    return result.ok &&
           result.inspection.status == wordpress::WordPressCredentialStatus::Complete &&
           result.inspection.mutability == wordpress::WordPressCredentialMutability::MutableDirectConstant &&
           result.inspection.credentials.db_name.state == wordpress::WordPressCredentialValueState::Present &&
           result.inspection.credentials.db_user.state == wordpress::WordPressCredentialValueState::Present &&
           result.inspection.credentials.db_host.state == wordpress::WordPressCredentialValueState::Present &&
           result.inspection.credentials.db_password.state == wordpress::WordPressCredentialValueState::Redacted;
}

} // namespace

DatabaseCredentialRotationAdapter::DatabaseCredentialRotationAdapter(site::SiteManager& sites,
                                                                     DatabaseManager& databases,
                                                                     wordpress::WordPressConfigService& wordpress_config,
                                                                     const wordpress::WordPressDatabaseCredentialResolver& wordpress_database_credentials,
                                                                     const wordpress::WordPressConfigUpdater& wordpress_updater,
                                                                     const MariaDBCredentialProvider& mariadb_provider,
                                                                     const wordpress::WordPressRuntimeVerifier& wordpress_verifier,
                                                                     logger::Logger& logger,
                                                                     PasswordGenerator password_generator,
                                                                     MetadataPersist metadata_persist,
                                                                     RuntimeApply runtime_apply,
                                                                     SiteHealthVerifier site_health_verifier)
    : sites_(sites)
    , databases_(databases)
    , wordpress_config_(wordpress_config)
    , wordpress_database_credentials_(wordpress_database_credentials)
    , wordpress_updater_(wordpress_updater)
    , mariadb_provider_(mariadb_provider)
    , wordpress_verifier_(wordpress_verifier)
    , logger_(logger)
    , password_generator_(std::move(password_generator))
    , metadata_persist_(std::move(metadata_persist))
    , runtime_apply_(std::move(runtime_apply))
    , site_health_verifier_(std::move(site_health_verifier)) {
}

std::string DatabaseCredentialRotationAdapter::key(const DatabaseCredentialRotationRequest& request) const {
    return std::to_string(request.site_id) + ":" + std::to_string(request.database_id);
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::step(bool success, std::string code, std::string message) const {
    DatabaseCredentialRotationStepResult result;
    result.success = success;
    result.code = std::move(code);
    result.message = std::move(message);
    return result;
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::fail(std::string code, std::string message) const {
    return step(false, std::move(code), std::move(message));
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::ok(std::string code, std::string message) const {
    return step(true, std::move(code), std::move(message));
}

DatabaseCredentialRotationAdapter::Context* DatabaseCredentialRotationAdapter::context_for(const DatabaseCredentialRotationRequest& request) {
    auto it = contexts_.find(key(request));
    return it == contexts_.end() ? nullptr : &it->second;
}

const DatabaseCredentialRotationAdapter::Context* DatabaseCredentialRotationAdapter::context_for(const DatabaseCredentialRotationRequest& request) const {
    auto it = contexts_.find(key(request));
    return it == contexts_.end() ? nullptr : &it->second;
}

void DatabaseCredentialRotationAdapter::erase_context(const DatabaseCredentialRotationRequest& request) {
    std::lock_guard<std::mutex> guard(mutex_);
    contexts_.erase(key(request));
}

bool DatabaseCredentialRotationAdapter::load_admin_credential(Context& context) const {
    const auto env_path = context.site_root / ".env";
    const auto root_password = env_value(env_path, "MYSQL_ROOT_PASSWORD");
    if (root_password.empty()) {
        return false;
    }
    context.mariadb_admin = {"root", root_password, "localhost"};
    return true;
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::inspect_wordpress(const DatabaseCredentialRotationRequest& request) {
    auto* site = sites_.find_by_id(request.site_id);
    if (site == nullptr) {
        return fail("site_not_found", "Site was not found");
    }
    auto* database = databases_.find(request.database_id);
    if (database == nullptr || database->site_id != request.site_id) {
        return fail("database_not_found", "Database was not found for this site");
    }
    const auto inspected = wordpress_config_.inspect_site(request.site_id);
    if (!wordpress_direct_complete(inspected)) {
        return fail(inspected.code.empty() ? "wordpress_unsupported" : inspected.code,
                    "WordPress credential source is not supported");
    }
    const auto target = wordpress_database_credentials_.resolve_target(inspected);
    if (!target.available) {
        return fail(target.status.empty() ? "database_target_unavailable" : target.status,
                    target.message.empty() ? "WordPress database target could not be resolved" : target.message);
    }
    if (target.database_id != request.database_id || database->id != target.database_id) {
        return fail("metadata_conflict", "WordPress credential metadata does not match the database record");
    }

    Context context;
    context.site_id = request.site_id;
    context.database_id = request.database_id;
    context.domain = site->domain;
    context.old_password = database->db_password;
    context.site_root = inspected.site_root;
    context.config_path = inspected.config_path;
    context.wordpress_result = inspected;
    context.mariadb_target = {(inspected.site_root / "docker-compose.yml").string(), "mariadb"};
    context.mariadb_identity = {database->db_user, "%"};
    if (context.old_password.empty() || !load_admin_credential(context)) {
        return fail("credential_source_unavailable", "Database credential source is unavailable");
    }

    std::lock_guard<std::mutex> guard(mutex_);
    contexts_[key(request)] = std::move(context);
    logger_.info("AUDIT", "WordPress credential rotation inspected site=" + std::to_string(request.site_id) +
                            " database=" + std::to_string(request.database_id));
    return ok("wordpress_inspected", "WordPress credential source inspected");
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::verify_old_credential(const DatabaseCredentialRotationRequest& request) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto* context = context_for(request);
    if (context == nullptr) return fail("rotation_context_missing", "Credential rotation context is missing");
    const auto result = mariadb_provider_.verify_password(context->mariadb_target, context->mariadb_identity, context->old_password);
    if (!result.success) {
        contexts_.erase(key(request));
        return fail(result.code.empty() ? "old_credential_verification_failed" : result.code,
                    "Existing database credential verification failed");
    }
    return ok("old_credential_verified", "Existing database credential verified");
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::assess_shared_user(const DatabaseCredentialRotationRequest& request) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto* context = context_for(request);
    if (context == nullptr) return fail("rotation_context_missing", "Credential rotation context is missing");
    auto provider_result = mariadb_provider_.detect_shared_user(context->mariadb_target, context->mariadb_admin, context->mariadb_identity);
    DatabaseCredentialRotationStepResult result;
    result.success = provider_result.success;
    result.code = provider_result.success ? "shared_user_assessed" : provider_result.code;
    result.message = provider_result.success ? "Shared credential risk assessed" : "Shared credential assessment failed";
    result.shared_assessment = provider_result.shared_assessment;
    if (!result.success || !mariadb_shared_credential_rotation_allowed(result.shared_assessment.state)) {
        contexts_.erase(key(request));
    }
    return result;
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::generate_password(const DatabaseCredentialRotationRequest& request) {
    const auto generated = password_generator_ ? password_generator_() : std::string{};
    if (generated.empty()) {
        erase_context(request);
        return fail("password_generation_failed", "Replacement database credential generation failed");
    }
    std::lock_guard<std::mutex> guard(mutex_);
    auto* context = context_for(request);
    if (context != nullptr) {
        context->new_password = generated;
    }
    auto result = ok("password_generated", "Replacement database credential generated");
    result.generated_password = generated;
    return result;
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::change_mariadb_password(const DatabaseCredentialRotationRequest& request,
                                                                                                const std::string& new_password) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto* context = context_for(request);
    if (context == nullptr) return fail("rotation_context_missing", "Credential rotation context is missing");
    const auto result = mariadb_provider_.change_password(context->mariadb_target, context->mariadb_admin, context->mariadb_identity, new_password);
    logger_.info("AUDIT", "WordPress credential rotation MariaDB mutation attempted site=" + std::to_string(request.site_id) +
                            " database=" + std::to_string(request.database_id));
    return result.success ? ok("mariadb_password_changed", "MariaDB password changed")
                          : fail(result.code.empty() ? "mariadb_password_change_failed" : result.code,
                                 "MariaDB password change failed");
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::update_wordpress_config(const DatabaseCredentialRotationRequest& request,
                                                                                                const std::string& new_password) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto* context = context_for(request);
    if (context == nullptr) return fail("rotation_context_missing", "Credential rotation context is missing");
    const auto updated = wordpress_updater_.update_file_atomic_validated(
        context->site_root,
        context->config_path,
        wordpress::WordPressConfigUpdateField::DbPassword,
        new_password,
        [](const std::filesystem::path&) {
            return wordpress::WordPressConfigValidationResult{true, "ok", "WordPress config syntax validation passed"};
        });
    if (!updated.success) {
        return fail(updated.code.empty() ? "wordpress_config_update_failed" : updated.code,
                    "WordPress config update failed");
    }
    context->config_rollback = updated.rollback;
    return ok("wordpress_config_updated", "WordPress config updated");
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::apply_runtime(const DatabaseCredentialRotationRequest& request) {
    auto* site = sites_.find_by_id(request.site_id);
    if (site == nullptr) return fail("site_not_found", "Site was not found");
    if (runtime_apply_ && !runtime_apply_(*site)) {
        return fail("runtime_apply_failed", "Runtime apply failed");
    }
    return ok("runtime_applied", "Runtime changes applied");
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::verify_new_credential(const DatabaseCredentialRotationRequest& request,
                                                                                              const std::string& new_password) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto* context = context_for(request);
    if (context == nullptr) return fail("rotation_context_missing", "Credential rotation context is missing");
    const auto result = mariadb_provider_.verify_password(context->mariadb_target, context->mariadb_identity, new_password);
    return result.success ? ok("new_credential_verified", "New database credential verified")
                          : fail(result.code.empty() ? "new_credential_verification_failed" : result.code,
                                 "New database credential verification failed");
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::verify_wordpress(const DatabaseCredentialRotationRequest& request) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto* context = context_for(request);
    if (context == nullptr) return fail("rotation_context_missing", "Credential rotation context is missing");
    auto verification_request = wordpress_config_.runtime_verification_request(context->wordpress_result);
    const auto result = wordpress_verifier_.verify_database_access(verification_request);
    return result.success ? ok("wordpress_verified", "WordPress database access verified")
                          : fail(result.code.empty() ? "wordpress_verification_failed" : result.code,
                                 "WordPress verification failed");
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::verify_site_health(const DatabaseCredentialRotationRequest& request) {
    auto* site = sites_.find_by_id(request.site_id);
    if (site == nullptr) return fail("site_not_found", "Site was not found");
    if (site_health_verifier_ && !site_health_verifier_(*site)) {
        return fail("site_health_verification_failed", "Site health verification failed");
    }
    return ok("site_health_verified", "Site health verified");
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::persist_metadata(const DatabaseCredentialRotationRequest& request,
                                                                                         const std::string& new_password) {
    std::string old_password;
    {
        std::lock_guard<std::mutex> guard(mutex_);
        const auto* context = context_for(request);
        if (context == nullptr) return fail("rotation_context_missing", "Credential rotation context is missing");
        old_password = context->old_password;
    }
    auto* database = databases_.find(request.database_id);
    if (database == nullptr || database->site_id != request.site_id) {
        return fail("database_not_found", "Database was not found for this site");
    }
    database->db_password = new_password;
    if (metadata_persist_ && !metadata_persist_()) {
        database->db_password = old_password;
        return fail("metadata_persist_failed", "Credential metadata persistence failed");
    }
    logger_.info("AUDIT", "WordPress credential rotation metadata persisted site=" + std::to_string(request.site_id) +
                            " database=" + std::to_string(request.database_id));
    erase_context(request);
    return ok("metadata_persisted", "Credential metadata persisted");
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::restore_mariadb_password(const DatabaseCredentialRotationRequest& request,
                                                                                                  const std::string& new_password) {
    (void)new_password;
    std::lock_guard<std::mutex> guard(mutex_);
    auto* context = context_for(request);
    if (context == nullptr) return fail("rotation_context_missing", "Credential rotation context is missing");
    const auto result = mariadb_provider_.restore_password(context->mariadb_target, context->mariadb_admin, context->mariadb_identity, context->old_password);
    return result.success ? ok("mariadb_password_restored", "MariaDB password restored")
                          : fail(result.code.empty() ? "mariadb_password_restore_failed" : result.code,
                                 "MariaDB password restore failed");
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::restore_wordpress_config(const DatabaseCredentialRotationRequest& request) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto* context = context_for(request);
    if (context == nullptr) return fail("rotation_context_missing", "Credential rotation context is missing");
    const auto result = wordpress_updater_.rollback_file(context->config_rollback);
    return result.success ? ok("wordpress_config_restored", "WordPress config restored")
                          : fail(result.code.empty() ? "wordpress_config_restore_failed" : result.code,
                                 "WordPress config restore failed");
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::restore_runtime(const DatabaseCredentialRotationRequest& request) {
    auto* site = sites_.find_by_id(request.site_id);
    if (site == nullptr) return fail("site_not_found", "Site was not found");
    if (runtime_apply_ && !runtime_apply_(*site)) {
        return fail("runtime_restore_failed", "Runtime restore failed");
    }
    return ok("runtime_restored", "Runtime restored");
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::verify_restored_wordpress(const DatabaseCredentialRotationRequest& request) {
    return verify_wordpress(request);
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::verify_restored_site_health(const DatabaseCredentialRotationRequest& request) {
    return verify_site_health(request);
}

DatabaseCredentialRotationStepResult DatabaseCredentialRotationAdapter::verify_restored_metadata(const DatabaseCredentialRotationRequest& request) {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto* context = context_for(request);
    if (context == nullptr) return fail("rotation_context_missing", "Credential rotation context is missing");
    const auto* database = databases_.find(request.database_id);
    if (database == nullptr || database->site_id != request.site_id) {
        return fail("database_not_found", "Database was not found for this site");
    }
    if (database->db_password != context->old_password) {
        return fail("metadata_restore_mismatch", "Credential metadata does not match restored database credential");
    }
    contexts_.erase(key(request));
    return ok("metadata_restored", "Credential metadata restored");
}

} // namespace containercp::database
