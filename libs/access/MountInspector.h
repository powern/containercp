#ifndef CONTAINERCP_ACCESS_MOUNT_INSPECTOR_H
#define CONTAINERCP_ACCESS_MOUNT_INSPECTOR_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace containercp::access {

enum class MountStatus { Ok, Absent, TargetMissing, PermissionDenied, InspectionFailed, DependencyUnavailable };

struct MountState {
    bool        mounted = false;
    uint64_t    mount_id = 0;
    uint64_t    parent_id = 0;
    std::string device;          // major:minor
    std::string bind_root;       // root field — path within the filesystem for bind mounts
    std::string target;          // mount point
    std::string options;         // mount options (comma-separated)
    std::vector<std::string> optional_fields;
    std::string fstype;
    std::string source;          // device or backing path (after-dash field)
    std::string super_options;   // superblock options
    bool        is_bind = false; // true when root != "/" and/or source is a real path (not a device)
    MountStatus status = MountStatus::Absent;
    std::string error_detail;
};

// Parse a single /proc/self/mountinfo line into a MountState.
// Returns MountState with status=Ok and all fields populated on success.
// Returns MountState with status=Absent and error_detail set on failure.
// `target_filter` — optional; if non-empty, only return a match for this exact target.
MountState parse_mountinfo_line(const std::string& line, const std::string& target_filter = "");

// Parse full /proc/self/mountinfo content for a given target.
MountState parse_mountinfo(const std::string& content, const std::string& target);

// Testable abstraction for inspecting mount state.
class MountInspector {
public:
    virtual ~MountInspector() = default;

    virtual MountState inspect(const std::string& path) const = 0;
};

// Production implementation using /proc/self/mountinfo
std::shared_ptr<MountInspector> make_real_mount_inspector();

} // namespace containercp::access

#endif
