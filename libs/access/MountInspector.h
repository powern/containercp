#ifndef CONTAINERCP_ACCESS_MOUNT_INSPECTOR_H
#define CONTAINERCP_ACCESS_MOUNT_INSPECTOR_H

#include <memory>
#include <string>

namespace containercp::runtime { class CommandExecutor; }

namespace containercp::access {

enum class MountStatus { Ok, Absent, TargetMissing, PermissionDenied, InspectionFailed, DependencyUnavailable };

struct MountState {
    bool        mounted = false;
    std::string source;          // canonical source
    std::string target;          // mount point
    std::string fstype;
    bool        is_bind = false;
    MountStatus status = MountStatus::Absent;
    std::string error_detail;
};

// Testable abstraction for inspecting mount state.
class MountInspector {
public:
    virtual ~MountInspector() = default;

    // Inspect whether `path` is a mountpoint and return mount identity.
    virtual MountState inspect(const std::string& path) const = 0;
};

// Production implementation using /proc/self/mountinfo
std::shared_ptr<MountInspector> make_real_mount_inspector(runtime::CommandExecutor& executor);

} // namespace containercp::access

#endif
