#ifndef CONTAINERCP_ACCESS_ACCESS_GRANT_H
#define CONTAINERCP_ACCESS_ACCESS_GRANT_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::access {

enum class Permission {
    READ_ONLY,
    READ_WRITE,
    DEPLOY
};

inline std::string permission_to_string(Permission p) {
    switch (p) {
        case Permission::READ_ONLY: return "read_only";
        case Permission::READ_WRITE: return "read_write";
        case Permission::DEPLOY: return "deploy";
    }
    return "read_only";
}

inline Permission permission_from_string(const std::string& s) {
    if (s == "read_write") return Permission::READ_WRITE;
    if (s == "deploy") return Permission::DEPLOY;
    return Permission::READ_ONLY;
}

struct AccessGrant : core::Resource {
    uint64_t access_user_id = 0;
    uint64_t site_id = 0;
    Permission permission = Permission::READ_WRITE;
};

} // namespace containercp::access

#endif // CONTAINERCP_ACCESS_ACCESS_GRANT_H
