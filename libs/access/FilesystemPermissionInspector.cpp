#include "access/FilesystemPermissionInspector.h"

#include "runtime/CommandExecutor.h"

#include <sstream>
#include <sys/stat.h>

namespace containercp::access {
namespace {

bool parse_getfacl(const std::string& output, const std::string& groupname,
                   FsPermissionState& state) {
    state.acl_present = false;
    state.acl_effective = true;

    std::istringstream ss(output);
    std::string line;
    std::string mask_perms;
    bool has_default_acl = false;

    while (std::getline(ss, line)) {
        if (line.empty() || line[0] == '#') continue;
        // Format: "group:name:rwx" or "default:group:name:r-x" etc.
        if (line.rfind("group:", 0) == 0) {
            auto col1 = line.find(':', 6);
            if (col1 != std::string::npos) {
                std::string name = line.substr(6, col1 - 6);
                std::string perms = line.substr(col1 + 1);
                if (name == groupname) {
                    state.acl_present = true;
                    state.acl_group = name;
                    state.acl_perms = perms;
                }
            }
        } else if (line.rfind("mask:", 0) == 0) {
            mask_perms = line.substr(5);
        } else if (line.rfind("default:", 0) == 0) {
            has_default_acl = true;
        }
    }

    // Check mask doesn't reduce effective permissions
    if (state.acl_present && !mask_perms.empty()) {
        for (char c : state.acl_perms) {
            if (mask_perms.find(c) == std::string::npos) {
                state.acl_effective = false;
                break;
            }
        }
    }

    return true;
}

} // namespace

class RealFilesystemPermissionInspector : public FilesystemPermissionInspector {
public:
    explicit RealFilesystemPermissionInspector(runtime::CommandExecutor& executor)
        : executor_(executor) {}

    FsPermissionState inspect(const std::string& path) const override {
        FsPermissionState s;
        struct stat st;
        if (::stat(path.c_str(), &st) != 0) return s;
        s.exists = true;
        s.owner_uid = static_cast<int>(st.st_uid);
        s.group_gid = static_cast<int>(st.st_gid);
        s.mode = static_cast<int>(st.st_mode & 07777);
        return s;
    }

    FsPermissionState inspect_acl(const std::string& path,
                                   const std::string& groupname) const override {
        FsPermissionState s = inspect(path);
        if (!s.exists) return s;

        auto result = executor_.run({"getfacl", "-cp", path});
        if (result.exit_code != 0) return s; // getfacl not available or error

        parse_getfacl(result.out, groupname, s);
        return s;
    }

private:
    runtime::CommandExecutor& executor_;
};

std::shared_ptr<FilesystemPermissionInspector>
make_real_filesystem_inspector(runtime::CommandExecutor& executor) {
    return std::make_shared<RealFilesystemPermissionInspector>(executor);
}

bool filesystem_acl_supported() {
    runtime::CommandExecutor exec;
    auto result = exec.run({"setfacl", "--version"});
    return result.exit_code == 0;
}

} // namespace containercp::access
