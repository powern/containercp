#include "access/FilesystemPermissionInspector.h"

#include "runtime/CommandExecutor.h"

#include <algorithm>
#include <sstream>
#include <sys/stat.h>
#include <cerrno>

namespace containercp::access {
namespace {

// Canonical POSIX permission format — exactly "rwx" triples per position.
bool valid_acl_perms(const std::string& s) {
    if (s.size() != 3) return false;
    static const char kAllowed[] = {'r', 'w', 'x', '-'};
    for (int i = 0; i < 3; ++i) {
        bool ok = false;
        for (char c : kAllowed) { if (s[i] == c) { ok = true; break; } }
        if (!ok) return false;
    }
    return true;
}

// Positional POSIX effective permission: named_perm[i] is effective only if
// mask_perm[i] also grants it. Otherwise it becomes '-'.
std::string effective(const std::string& named, const std::string& mask) {
    if (mask.empty() || mask == "rwx") return named; // no mask or full mask
    std::string out = "---";
    for (int i = 0; i < 3; ++i) {
        if (named[i] == mask[i] && named[i] != '-') out[i] = named[i];
    }
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
    std::string access_mask;
    std::string default_mask;
    bool access_mask_seen = false;
    bool default_mask_seen = false;
    // Member field moved to struct level for duplicate tracking
    bool _access_group_seen = false;
    bool _default_group_seen = false;

    bool parse(const std::string& output, const std::string& target_group) {
        if (output.rfind("getfacl:", 0) == 0) {
            status = InspectionStatus::AclParseFailed;
            error_detail = "getfacl error in output";
            return false;
        }

        std::istringstream ss(output);
        std::string line;
        int line_no = 0;

        while (std::getline(ss, line)) {
            line_no++;
            if (line.empty() || line[0] == '#') continue;

            if (line.rfind("getfacl:", 0) == 0) {
                status = InspectionStatus::AclParseFailed;
                error_detail = "getfacl error line " + std::to_string(line_no);
                return false;
            }

            // Parse: "group:<name>:<perms>"
            if (line.rfind("group:", 0) == 0 && line.rfind("default:group:", 0) != 0) {
                auto c1 = line.find(':', 6);
                if (c1 == std::string::npos) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad group line " + std::to_string(line_no); return false; }
                std::string name = line.substr(6, c1 - 6);
                std::string perms = line.substr(c1 + 1);
                if (!valid_acl_perms(perms)) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad perms line " + std::to_string(line_no); return false; }
                if (name == target_group) {
                    if (_access_group_seen) { status = InspectionStatus::MalformedAclOutput; error_detail = "duplicate access group line " + std::to_string(line_no); return false; }
                    _access_group_seen = true;
                    access_found = true;
                    access_group = name;
                    access_perms = perms;
                }
            }
            // Parse: "default:group:<name>:<perms>"
            else if (line.rfind("default:group:", 0) == 0) {
                auto c1 = line.find(':', 14);
                if (c1 == std::string::npos) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad default group line " + std::to_string(line_no); return false; }
                std::string name = line.substr(14, c1 - 14);
                std::string perms = line.substr(c1 + 1);
                if (!valid_acl_perms(perms)) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad default perms line " + std::to_string(line_no); return false; }
                if (name == target_group) {
                    if (_default_group_seen) { status = InspectionStatus::MalformedAclOutput; error_detail = "duplicate default group line " + std::to_string(line_no); return false; }
                    _default_group_seen = true;
                    default_found = true;
                    default_group = name;
                    default_perms = perms;
                }
            }
            // Parse: "mask:<perms>"
            else if (line.rfind("mask:", 0) == 0 && line.rfind("default:mask:", 0) != 0) {
                if (access_mask_seen) { status = InspectionStatus::MalformedAclOutput; error_detail = "duplicate mask line " + std::to_string(line_no); return false; }
                if (line.size() < 8) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad mask line " + std::to_string(line_no); return false; }
                std::string m = line.substr(5);
                if (!valid_acl_perms(m)) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad mask perms line " + std::to_string(line_no); return false; }
                access_mask_seen = true;
                access_mask = m;
            }
            // Parse: "default:mask:<perms>"
            else if (line.rfind("default:mask:", 0) == 0) {
                if (default_mask_seen) { status = InspectionStatus::MalformedAclOutput; error_detail = "duplicate default mask line " + std::to_string(line_no); return false; }
                if (line.size() < 16) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad default mask line " + std::to_string(line_no); return false; }
                std::string m = line.substr(13);
                if (!valid_acl_perms(m)) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad default mask perms line " + std::to_string(line_no); return false; }
                default_mask_seen = true;
                default_mask = m;
            }
            // user::, other::, default:user::, default:other:: — allowed but ignored
            else if (line.rfind("user:", 0) == 0 ||
                     line.rfind("other:", 0) == 0 ||
                     line.rfind("default:user:", 0) == 0 ||
                     line.rfind("default:other:", 0) == 0) {
                // permitted, skip
            }
            else {
                status = InspectionStatus::MalformedAclOutput;
                error_detail = "unknown ACL line " + std::to_string(line_no) + ": " + line.substr(0, 40);
                return false;
            }
        }
        return true;
    }
};

InspectionStatus classify_getfacl_error(int exit_code, const std::string& stderr_out) {
    if (stderr_out.find("Permission denied") != std::string::npos ||
        stderr_out.find("EACCES") != std::string::npos)
        return InspectionStatus::AccessDenied;
    if (stderr_out.find("not supported") != std::string::npos ||
        stderr_out.find("Operation not supported") != std::string::npos)
        return InspectionStatus::AclUnsupported;
    if (exit_code == 127 || stderr_out.find("not found") != std::string::npos ||
        stderr_out.find("No such file") != std::string::npos)
        return InspectionStatus::AclToolMissing;
    return InspectionStatus::AclParseFailed;
}

} // namespace

class RealFilesystemPermissionInspector : public FilesystemPermissionInspector {
public:
    explicit RealFilesystemPermissionInspector(runtime::CommandExecutor& executor)
        : executor_(executor) {}

    FsPermissionState inspect(const std::string& path) const override {
        FsPermissionState s;
        struct stat st;
        if (::lstat(path.c_str(), &st) != 0) {
            switch (errno) {
            case ENOENT:  s.acl_status = InspectionStatus::PathMissing; break;
            case EACCES:  s.acl_status = InspectionStatus::AccessDenied; break;
            default:      s.acl_status = InspectionStatus::PathInspectionFailed; break;
            }
            return s;
        }
        s.exists = true;
        s.is_symlink = S_ISLNK(st.st_mode);
        s.owner_uid = static_cast<int>(st.st_uid);
        s.group_gid = static_cast<int>(st.st_gid);
        s.mode = static_cast<int>(st.st_mode & 07777);
        s.acl_status = InspectionStatus::Ok;
        return s;
    }

    FsPermissionState inspect_acl(const std::string& path,
                                   const std::string& groupname) const override {
        FsPermissionState s = inspect(path);
        if (!s.exists) return s; // status already set by inspect

        auto result = executor_.run({"getfacl", "-cp", path});
        if (result.exit_code != 0) {
            s.acl_status = classify_getfacl_error(result.exit_code, result.err);
            return s;
        }

        AclParser parser;
        if (!parser.parse(result.out, groupname)) {
            s.acl_status = parser.status;
            s.acl_error_detail = parser.error_detail;
            return s;
        }

        s.access_acl_present = parser.access_found;
        s.access_acl_group = parser.access_group;
        s.access_acl_perms = parser.access_perms;
        s.effective_perms = effective(parser.access_perms, parser.access_mask);

        s.default_acl_present = parser.default_found;
        s.default_acl_group = parser.default_group;
        s.default_acl_perms = parser.default_perms;
        s.default_effective_perms = effective(parser.default_perms, parser.default_mask);

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
    if (exec.run({"setfacl", "--version"}).exit_code != 0)
        return InspectionStatus::AclToolMissing;
    if (exec.run({"getfacl", "--version"}).exit_code != 0)
        return InspectionStatus::AclToolMissing;

    auto result = exec.run({"getfacl", "-cp", test_path});
    if (result.exit_code != 0)
        return classify_getfacl_error(result.exit_code, result.err);

    return InspectionStatus::Ok;
}

} // namespace containercp::access
