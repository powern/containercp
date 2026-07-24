#ifndef CONTAINERCP_ACCESS_FILESYSTEM_PERMISSION_INSPECTOR_H
#define CONTAINERCP_ACCESS_FILESYSTEM_PERMISSION_INSPECTOR_H

#include <memory>
#include <string>

namespace containercp::runtime { class CommandExecutor; }

namespace containercp::access {

// Typed inspection status — never use a free-form string alone.
enum class InspectionStatus {
    Ok = 0,
    PathMissing,
    PathInspectionFailed,
    AccessDenied,
    AclToolMissing,
    AclUnsupported,
    AclParseFailed,
    MalformedAclOutput,
};

struct FsPermissionState {
    bool          exists = false;
    bool          is_symlink = false;
    int           owner_uid = -1;
    int           group_gid = -1;
    int           mode = 0;

    // Access ACL for a named group
    bool          access_acl_present = false;
    std::string   access_acl_group;
    std::string   access_acl_perms;       // e.g. "r-x" as configured
    std::string   effective_perms;        // after masking: named_perms & mask

    // Default ACL for a named group (directories only)
    bool          default_acl_present = false;
    std::string   default_acl_group;
    std::string   default_acl_perms;
    std::string   default_effective_perms;

    // ACL inspection status
    InspectionStatus acl_status = InspectionStatus::Ok;
    std::string      acl_error_detail;   // bounded, for logs only
};

class FilesystemPermissionInspector {
public:
    virtual ~FilesystemPermissionInspector() = default;
    virtual FsPermissionState inspect(const std::string& path) const = 0;
    virtual FsPermissionState inspect_acl(const std::string& path,
                                           const std::string& groupname) const = 0;
};

std::shared_ptr<FilesystemPermissionInspector>
make_real_filesystem_inspector(runtime::CommandExecutor& executor);

// Returns Ok if setfacl+getfacl exist AND target path supports ACLs.
InspectionStatus check_acl_capability(const std::string& test_path);

} // namespace containercp::access

#endif
