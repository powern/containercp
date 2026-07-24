#ifndef CONTAINERCP_ACCESS_FILESYSTEM_PERMISSION_INSPECTOR_H
#define CONTAINERCP_ACCESS_FILESYSTEM_PERMISSION_INSPECTOR_H

#include <memory>
#include <string>

namespace containercp::runtime { class CommandExecutor; }

namespace containercp::access {

// --- Typed inspection status ---
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

// --- POSIX ACL parser ---
// Canonical permission: pos0=r/-, pos1=w/-, pos2=x/-
bool valid_acl_perms(const std::string& s);

// Positional effective: named[i] is effective only if mask[i] also grants it
std::string effective_acl(const std::string& named, const std::string& mask);
InspectionStatus classify_getfacl_error(int exit_code, const std::string& stderr_out);

struct AclState {
    bool access_present = false;
    std::string access_group;
    std::string access_perms;       // e.g. "r-x"
    std::string effective_perms;    // after mask
    bool default_present = false;
    std::string default_group;
    std::string default_perms;
    std::string default_effective;
    std::string access_mask;
    std::string default_mask;
};

// Parse getfacl output. Returns false on error, sets status.
bool parse_getfacl(const std::string& output, const std::string& target_group,
                   AclState& state, InspectionStatus& status, std::string& error_detail);

struct FsPermissionState {
    bool          exists = false;
    bool          is_symlink = false;
    int           owner_uid = -1;
    int           group_gid = -1;
    int           mode = 0;
    InspectionStatus acl_status = InspectionStatus::Ok;
    std::string      acl_error_detail;
    AclState     acl;           // access + default ACL parsed state
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
