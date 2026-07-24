#include "access/FilesystemPermissionInspector.h"

#include "runtime/CommandExecutor.h"

#include <sstream>
#include <sys/stat.h>

namespace containercp::access {
namespace {

bool valid_acl_perms(const std::string& s) {
    if (s.empty() || s.size() > 3) return false;
    for (char c : s) { if (c != 'r' && c != 'w' && c != 'x' && c != '-') return false; }
    return true;
}

std::string effective(const std::string& named, const std::string& mask) {
    if (mask.empty()) return named;
    std::string out;
    for (char c : named) out.push_back((mask.find(c) != std::string::npos) ? c : '-');
    return out;
}

struct AclParser {
    InspectionStatus status = InspectionStatus::Ok;
    std::string error_detail;

    bool access_found = false;
    std::string access_group;
    std::string access_perms;

    bool default_found = false;
    std::string default_group;
    std::string default_perms;

    std::string mask_perms;

    bool parse(const std::string& output, const std::string& target_group) {
        // Check for getfacl errors in output
        if (output.rfind("getfacl:", 0) == 0) {
            status = InspectionStatus::AclToolMissing;
            error_detail = output.substr(0, 120);
            return false;
        }

        std::istringstream ss(output);
        std::string line;
        int line_no = 0;

        while (std::getline(ss, line)) {
            line_no++;
            if (line.empty() || line[0] == '#') continue;

            // Detect error lines mixed in
            if (line.rfind("getfacl:", 0) == 0) {
                status = InspectionStatus::AclToolMissing;
                error_detail = line.substr(0, 120);
                return false;
            }

            // Parse access ACL: "group:<name>:<perms>"
            if (line.rfind("group:", 0) == 0) {
                auto c1 = line.find(':', 6);
                if (c1 == std::string::npos) { status = InspectionStatus::MalformedAclOutput; error_detail = "line " + std::to_string(line_no); return false; }
                std::string name = line.substr(6, c1 - 6);
                std::string perms = line.substr(c1 + 1);
                if (!valid_acl_perms(perms)) { status = InspectionStatus::MalformedAclOutput; error_detail = "invalid perms line " + std::to_string(line_no); return false; }
                if (name == target_group) {
                    access_found = true;
                    access_group = name;
                    access_perms = perms;
                }
            }
            // Parse default ACL: "default:group:<name>:<perms>"
            else if (line.rfind("default:group:", 0) == 0) {
                auto c1 = line.find(':', 14);
                if (c1 == std::string::npos) { status = InspectionStatus::MalformedAclOutput; error_detail = "line " + std::to_string(line_no); return false; }
                std::string name = line.substr(14, c1 - 14);
                std::string perms = line.substr(c1 + 1);
                if (!valid_acl_perms(perms)) { status = InspectionStatus::MalformedAclOutput; error_detail = "invalid default perms line " + std::to_string(line_no); return false; }
                if (name == target_group) {
                    default_found = true;
                    default_group = name;
                    default_perms = perms;
                }
            }
            // Parse mask: "mask:<perms>"
            else if (line.rfind("mask:", 0) == 0 && line.size() > 5) {
                std::string m = line.substr(5);
                if (!valid_acl_perms(m)) { status = InspectionStatus::MalformedAclOutput; error_detail = "invalid mask line " + std::to_string(line_no); return false; }
                mask_perms = m;
            }
        }
        return true;
    }
};

} // namespace

class RealFilesystemPermissionInspector : public FilesystemPermissionInspector {
public:
    explicit RealFilesystemPermissionInspector(runtime::CommandExecutor& executor)
        : executor_(executor) {}

    FsPermissionState inspect(const std::string& path) const override {
        FsPermissionState s;
        struct stat st;
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
            s.acl_status = InspectionStatus::PathMissing;
            return s;
        }

        auto result = executor_.run({"getfacl", "-cp", path});
        if (result.exit_code != 0) {
            s.acl_status = InspectionStatus::AclToolMissing;
            s.acl_error_detail = "exit=" + std::to_string(result.exit_code);
            return s;
        }

        AclParser parser;
        if (!parser.parse(result.out, groupname)) {
            s.acl_status = parser.status;
            s.acl_error_detail = parser.error_detail;
            return s;
        }

        // Access ACL
        s.access_acl_present = parser.access_found;
        s.access_acl_group = parser.access_group;
        s.access_acl_perms = parser.access_perms;
        s.effective_perms = effective(parser.access_perms, parser.mask_perms);

        // Default ACL
        s.default_acl_present = parser.default_found;
        s.default_acl_group = parser.default_group;
        s.default_acl_perms = parser.default_perms;
        s.default_effective_perms = effective(parser.default_perms, parser.mask_perms);

        s.acl_status = InspectionStatus::Ok;
        return s;
    }

private:
    runtime::CommandExecutor& executor_;
};

std::shared_ptr<FilesystemPermissionInspector>
make_real_filesystem_inspector(runtime::CommandExecutor& executor) {
    return std::make_shared<RealFilesystemPermissionInspector>(executor);
}

InspectionStatus check_acl_capability(const std::string& test_path) {
    runtime::CommandExecutor exec;
    if (exec.run({"setfacl", "--version"}).exit_code != 0) return InspectionStatus::AclToolMissing;
    if (exec.run({"getfacl", "--version"}).exit_code != 0) return InspectionStatus::AclToolMissing;

    // Verify ACLs work on the target path
    auto result = exec.run({"getfacl", "-cp", test_path});
    if (result.exit_code != 0) {
        if (result.err.find("not supported") != std::string::npos ||
            result.err.find("Operation not supported") != std::string::npos)
            return InspectionStatus::AclUnsupported;
        return InspectionStatus::AclToolMissing;
    }
    return InspectionStatus::Ok;
}

} // namespace containercp::access
