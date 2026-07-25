#ifndef CONTAINERCP_STORAGE_GRANT_LIFECYCLE_STATE_H
#define CONTAINERCP_STORAGE_GRANT_LIFECYCLE_STATE_H

#include <cstdint>
#include <string>

namespace containercp::storage {

// Persisted lifecycle state for each SFTP grant.
// Composite key: (access_user_id, site_id).
struct GrantLifecycleState {
    uint64_t access_user_id = 0;
    uint64_t site_id = 0;
    std::string permission;   // read_only, read_write, deploy
    std::string state;        // pending, applying, active, revoking, error
    std::string last_error;
    std::string created_at;
    std::string updated_at;
};

inline bool valid_grant_state(const std::string& s) {
    return s == "pending" || s == "applying" || s == "active"
        || s == "revoking" || s == "error";
}

inline bool valid_grant_permission(const std::string& s) {
    return s == "read_only" || s == "read_write" || s == "deploy";
}

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_GRANT_LIFECYCLE_STATE_H
