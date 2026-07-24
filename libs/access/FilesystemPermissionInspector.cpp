#include "access/FilesystemPermissionInspector.h"

#include "runtime/CommandExecutor.h"

#include <sstream>
#include <sys/stat.h>

namespace containercp::access {
namespace {

// Parse getfacl output and populate state.acl_* fields.
// Returns false only on parse failure (malformed output).
// Sets acl_error on access problems.
bool parse_getfacl(const std::string& output, const std::string& groupname,
                   FsPermissionState& state) {
    state.acl_present = false;
    state.acl_error = false;
    state.acl_effective = true;
    state.acl_group.clear();
    state.acl_perms.clear();

    // Check for getfacl error messages in output
    if (output.rfind("getfacl:", 0) == 0 || output.find("\ngetfacl:") != std::string::npos) {
        state.acl_error = true;
        state.acl_error_msg = output;
        return false;
    }

    std::istringstream ss(output);
    std::string line;
    std::string mask_perms;
    bool found_group = false;

    while (std::getline(ss, line)) {
        // Skip comments and blanks
        if (line.empty() || line[0] == '#') continue;

        // Detect error lines mixed into output
        if (line.rfind("getfacl:", 0) == 0) {
            state.acl_error = true;
            state.acl_error_msg = line;
            return false;
        }

        // Access ACL entry: "group:<name>:<perms>"
        if (line.rfind("group:", 0) == 0) {
            auto col1 = line.find(':', 6);
            if (col1 != std::string::npos) {
                std::string name = line.substr(6, col1 - 6);
                std::string perms = line.substr(col1 + 1);
                if (name == groupname) {
                    state.acl_present = true;
                    state.acl_group = name;
                    state.acl_perms = perms;
                    found_group = true;
                }
            }
        }
        // Mask entry: "mask:<perms>"
        else if (line.rfind("mask:", 0) == 0 && line.size() > 5) {
            mask_perms = line.substr(5);
        }
    }

    // Check mask doesn't reduce effective permissions
    if (found_group && !mask_perms.empty()) {
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
        // Use lstat to detect symlinks without following them
        if (::lstat(path.c_str(), &st) != 0) return s;
        s.exists = true;
        s.is_symlink = S_ISLNK(st.st_mode);
        s.owner_uid = static_cast<int>(st.st_uid);
        s.group_gid = static_cast<int>(st.st_gid);
        s.mode = static_cast<int>(st.st_mode & 07777);
        return s;
    }

    FsPermissionState inspect_acl(const std::string& path,
                                   const std::string& groupname) const override {
        FsPermissionState s = inspect(path);
        if (!s.exists) {
            s.acl_error = true;
            s.acl_error_msg = "path does not exist";
            return s;
        }

        auto result = executor_.run({"getfacl", "-cp", path});
        if (result.exit_code != 0) {
            s.acl_error = true;
            s.acl_error_msg = "getfacl failed (exit=" + std::to_string(result.exit_code) +
                               "): " + result.err;
            return s;
        }

        if (!parse_getfacl(result.out, groupname, s)) {
            // parse_getfacl already set acl_error
            return s;
        }

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
    // Check both setfacl and getfacl availability
    auto r1 = exec.run({"setfacl", "--version"});
    auto r2 = exec.run({"getfacl", "--version"});
    return r1.exit_code == 0 && r2.exit_code == 0;
}

} // namespace containercp::access
