#ifndef CONTAINERCP_ACCESS_FILESYSTEM_PERMISSION_INSPECTOR_H
#define CONTAINERCP_ACCESS_FILESYSTEM_PERMISSION_INSPECTOR_H

#include <string>

namespace containercp::access {

struct FsPermissionState {
    bool   exists = false;
    int    owner_uid = -1;
    int    group_gid = -1;
    int    mode = 0;           // octal e.g. 0770
    bool   acl_present = false; // specific named-group ACL exists
    std::string acl_group;
    std::string acl_perms;     // e.g. "r-x"
    bool   acl_effective = true; // mask not reducing effective perms
};

// Testable abstraction for reading filesystem permission state.
// Real implementation uses stat() + getfacl.
class FilesystemPermissionInspector {
public:
    virtual ~FilesystemPermissionInspector() = default;

    virtual FsPermissionState inspect(const std::string& path) const = 0;
    virtual FsPermissionState inspect_acl(const std::string& path,
                                           const std::string& groupname) const = 0;
};

} // namespace containercp::access

#endif
