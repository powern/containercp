#ifndef CONTAINERCP_ACCESS_FILESYSTEM_PERMISSION_INSPECTOR_H
#define CONTAINERCP_ACCESS_FILESYSTEM_PERMISSION_INSPECTOR_H

#include <memory>
#include <string>

namespace containercp::runtime { class CommandExecutor; }

namespace containercp::access {

struct FsPermissionState {
    bool   exists = false;
    int    owner_uid = -1;
    int    group_gid = -1;
    int    mode = 0;
    bool   acl_present = false;
    std::string acl_group;
    std::string acl_perms;
    bool   acl_effective = true;
};

class FilesystemPermissionInspector {
public:
    virtual ~FilesystemPermissionInspector() = default;
    virtual FsPermissionState inspect(const std::string& path) const = 0;
    virtual FsPermissionState inspect_acl(const std::string& path,
                                           const std::string& groupname) const = 0;
};

// Factory: creates the real stat+getfacl-based inspector.
std::shared_ptr<FilesystemPermissionInspector>
make_real_filesystem_inspector(runtime::CommandExecutor& executor);

// Check if the host supports ACL operations (setfacl available).
bool filesystem_acl_supported();

} // namespace containercp::access

#endif
