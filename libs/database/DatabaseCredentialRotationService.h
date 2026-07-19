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

class DatabaseCredentialRotationService {
public:
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
};

std::string database_credential_rotation_state_to_string(DatabaseCredentialRotationState state);

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_CREDENTIAL_ROTATION_SERVICE_H
