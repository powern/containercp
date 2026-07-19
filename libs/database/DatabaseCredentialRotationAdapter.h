#ifndef CONTAINERCP_DATABASE_DATABASE_CREDENTIAL_ROTATION_ADAPTER_H
#define CONTAINERCP_DATABASE_DATABASE_CREDENTIAL_ROTATION_ADAPTER_H

#include "database/DatabaseCredentialRotationService.h"
#include "database/DatabaseManager.h"
#include "database/MariaDBCredentialProvider.h"
#include "logger/Logger.h"
#include "site/SiteManager.h"
#include "wordpress/WordPressConfigService.h"
#include "wordpress/WordPressConfigUpdater.h"
#include "wordpress/WordPressDatabaseCredentialResolver.h"
#include "wordpress/WordPressRuntimeVerifier.h"

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace containercp::database {

class DatabaseCredentialRotationAdapter : public DatabaseCredentialRotationDependencies {
public:
    using PasswordGenerator = std::function<std::string()>;
    using MetadataPersist = std::function<bool()>;
    using RuntimeApply = std::function<bool(const site::Site&)>;
    using SiteHealthVerifier = std::function<bool(const site::Site&)>;

    DatabaseCredentialRotationAdapter(site::SiteManager& sites,
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
                                      SiteHealthVerifier site_health_verifier);

    DatabaseCredentialRotationStepResult inspect_wordpress(const DatabaseCredentialRotationRequest& request) override;
    DatabaseCredentialRotationStepResult verify_old_credential(const DatabaseCredentialRotationRequest& request) override;
    DatabaseCredentialRotationStepResult assess_shared_user(const DatabaseCredentialRotationRequest& request) override;
    DatabaseCredentialRotationStepResult generate_password(const DatabaseCredentialRotationRequest& request) override;
    DatabaseCredentialRotationStepResult change_mariadb_password(const DatabaseCredentialRotationRequest& request,
                                                                 const std::string& new_password) override;
    DatabaseCredentialRotationStepResult update_wordpress_config(const DatabaseCredentialRotationRequest& request,
                                                                 const std::string& new_password) override;
    DatabaseCredentialRotationStepResult apply_runtime(const DatabaseCredentialRotationRequest& request) override;
    DatabaseCredentialRotationStepResult verify_new_credential(const DatabaseCredentialRotationRequest& request,
                                                               const std::string& new_password) override;
    DatabaseCredentialRotationStepResult verify_wordpress(const DatabaseCredentialRotationRequest& request) override;
    DatabaseCredentialRotationStepResult verify_site_health(const DatabaseCredentialRotationRequest& request) override;
    DatabaseCredentialRotationStepResult persist_metadata(const DatabaseCredentialRotationRequest& request,
                                                          const std::string& new_password) override;
    DatabaseCredentialRotationStepResult restore_mariadb_password(const DatabaseCredentialRotationRequest& request,
                                                                  const std::string& new_password) override;
    DatabaseCredentialRotationStepResult restore_wordpress_config(const DatabaseCredentialRotationRequest& request) override;
    DatabaseCredentialRotationStepResult restore_runtime(const DatabaseCredentialRotationRequest& request) override;

private:
    struct Context {
        uint64_t site_id = 0;
        uint64_t database_id = 0;
        std::string domain;
        std::string old_password;
        std::string new_password;
        std::filesystem::path site_root;
        std::filesystem::path config_path;
        wordpress::WordPressConfigServiceResult wordpress_result;
        wordpress::WordPressConfigRollbackHandle config_rollback;
        MariaDBConnectionTarget mariadb_target;
        MariaDBAdminCredential mariadb_admin;
        MariaDBUserIdentity mariadb_identity;
    };

    std::string key(const DatabaseCredentialRotationRequest& request) const;
    DatabaseCredentialRotationStepResult step(bool success, std::string code, std::string message) const;
    DatabaseCredentialRotationStepResult fail(std::string code, std::string message) const;
    DatabaseCredentialRotationStepResult ok(std::string code, std::string message) const;
    Context* context_for(const DatabaseCredentialRotationRequest& request);
    const Context* context_for(const DatabaseCredentialRotationRequest& request) const;
    void erase_context(const DatabaseCredentialRotationRequest& request);
    bool load_admin_credential(Context& context) const;

    site::SiteManager& sites_;
    DatabaseManager& databases_;
    wordpress::WordPressConfigService& wordpress_config_;
    const wordpress::WordPressDatabaseCredentialResolver& wordpress_database_credentials_;
    const wordpress::WordPressConfigUpdater& wordpress_updater_;
    const MariaDBCredentialProvider& mariadb_provider_;
    const wordpress::WordPressRuntimeVerifier& wordpress_verifier_;
    logger::Logger& logger_;
    PasswordGenerator password_generator_;
    MetadataPersist metadata_persist_;
    RuntimeApply runtime_apply_;
    SiteHealthVerifier site_health_verifier_;
    mutable std::mutex mutex_;
    std::map<std::string, Context> contexts_;
};

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_CREDENTIAL_ROTATION_ADAPTER_H
