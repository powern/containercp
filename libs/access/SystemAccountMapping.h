#ifndef CONTAINERCP_ACCESS_SYSTEM_ACCOUNT_MAPPING_H
#define CONTAINERCP_ACCESS_SYSTEM_ACCOUNT_MAPPING_H

#include <cstdint>
#include <string>

namespace containercp::access {

// Persisted mapping from a ContainerCP entity to a managed Linux identity.
// Composite key: (entity_type, entity_id).
// Proves ownership before any destructive OS operation.
struct SystemAccountMapping {
    std::string entity_type;   // "access_user"
    uint64_t    entity_id = 0; // AccessUser.id
    int         uid = 0;       // allocated UID (0 if not yet allocated)
    int         gid = 0;       // allocated GID
    std::string username;      // canonical system username
    std::string groupname;     // canonical primary group name
    std::string state;         // "active", "provisioning", "removing", "error"
    std::string created_at;
    std::string updated_at;
};

} // namespace containercp::access

#endif
