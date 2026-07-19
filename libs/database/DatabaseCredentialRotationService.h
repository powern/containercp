#ifndef CONTAINERCP_DATABASE_DATABASE_CREDENTIAL_ROTATION_SERVICE_H
#define CONTAINERCP_DATABASE_DATABASE_CREDENTIAL_ROTATION_SERVICE_H

#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace containercp::database {

enum class DatabaseCredentialRotationState {
    NotStarted,
    LockAcquired,
    InspectingWordPress,
    VerifyingOldCredential,
    GeneratingPassword,
    ChangingMariaDBPassword,
    UpdatingWordPressConfig,
    ApplyingRuntime,
    VerifyingMariaDBPassword,
    VerifyingWordPress,
    VerifyingSiteHealth,
    PersistingMetadata,
    Completed,
    Compensating,
    Compensated,
    ManualRecoveryRequired,
    Failed
};

struct DatabaseCredentialRotationRequest {
    uint64_t site_id = 0;
    uint64_t database_id = 0;
    std::string confirmation;
};

struct DatabaseCredentialRotationEvent {
    DatabaseCredentialRotationState state = DatabaseCredentialRotationState::NotStarted;
    std::string code;
    std::string message;
};

struct DatabaseCredentialRotationResult {
    bool success = false;
    DatabaseCredentialRotationState final_state = DatabaseCredentialRotationState::NotStarted;
    std::string code;
    std::string message;
    std::vector<DatabaseCredentialRotationEvent> events;
};

struct DatabaseCredentialRotationStepResult {
    bool success = false;
    std::string code;
    std::string message;
    std::string generated_password;
};

class DatabaseCredentialRotationDependencies {
public:
    virtual ~DatabaseCredentialRotationDependencies() = default;

    virtual DatabaseCredentialRotationStepResult inspect_wordpress(const DatabaseCredentialRotationRequest& request) = 0;
    virtual DatabaseCredentialRotationStepResult verify_old_credential(const DatabaseCredentialRotationRequest& request) = 0;
    virtual DatabaseCredentialRotationStepResult generate_password(const DatabaseCredentialRotationRequest& request) = 0;
    virtual DatabaseCredentialRotationStepResult change_mariadb_password(const DatabaseCredentialRotationRequest& request,
                                                                         const std::string& new_password) = 0;
    virtual DatabaseCredentialRotationStepResult update_wordpress_config(const DatabaseCredentialRotationRequest& request,
                                                                         const std::string& new_password) = 0;
    virtual DatabaseCredentialRotationStepResult apply_runtime(const DatabaseCredentialRotationRequest& request) = 0;
    virtual DatabaseCredentialRotationStepResult verify_new_credential(const DatabaseCredentialRotationRequest& request,
                                                                       const std::string& new_password) = 0;
    virtual DatabaseCredentialRotationStepResult verify_wordpress(const DatabaseCredentialRotationRequest& request) = 0;
    virtual DatabaseCredentialRotationStepResult verify_site_health(const DatabaseCredentialRotationRequest& request) = 0;
    virtual DatabaseCredentialRotationStepResult persist_metadata(const DatabaseCredentialRotationRequest& request,
                                                                  const std::string& new_password) = 0;
};

class DatabaseCredentialRotationService {
public:
    DatabaseCredentialRotationService() = default;
    explicit DatabaseCredentialRotationService(DatabaseCredentialRotationDependencies& dependencies);

    DatabaseCredentialRotationResult rotate(const DatabaseCredentialRotationRequest& request);
    bool is_locked(uint64_t site_id, uint64_t database_id) const;

private:
    class OperationLock {
    public:
        OperationLock(DatabaseCredentialRotationService& service, uint64_t site_id, uint64_t database_id);
        ~OperationLock();

        OperationLock(const OperationLock&) = delete;
        OperationLock& operator=(const OperationLock&) = delete;

        bool acquired() const { return acquired_; }

    private:
        DatabaseCredentialRotationService& service_;
        std::string key_;
        bool acquired_ = false;
    };

    static std::string lock_key(uint64_t site_id, uint64_t database_id);

    void release_lock(const std::string& key);

    mutable std::mutex mutex_;
    std::set<std::string> active_locks_;
    DatabaseCredentialRotationDependencies* dependencies_ = nullptr;
};

std::string database_credential_rotation_state_to_string(DatabaseCredentialRotationState state);

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_CREDENTIAL_ROTATION_SERVICE_H
