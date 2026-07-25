#ifndef CONTAINERCP_STORAGE_MANAGED_MOUNT_STATE_H
#define CONTAINERCP_STORAGE_MANAGED_MOUNT_STATE_H

#include <cstdint>
#include <string>

namespace containercp::storage {

// Persisted mount record for a managed SFTP bind mount.
// Composite key: (access_user_id, site_id).
// Serves as the source of truth for expected mounts during reconciliation.
struct ManagedMountState {
    uint64_t access_user_id = 0;
    uint64_t site_id = 0;
    std::string domain;
    std::string source_path;
    std::string target_path;
    std::string state;       // pending, applying, active, removing, error
    std::string last_error;
    std::string created_at;
    std::string updated_at;
};

// Valid lifecycle states for ManagedMountState::state.
inline bool valid_mount_state(const std::string& s) {
    return s == "pending" || s == "applying" || s == "active"
        || s == "removing" || s == "error";
}

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_MANAGED_MOUNT_STATE_H
