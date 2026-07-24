#include "access/FilesystemPermissionInspector.h"

#include "runtime/CommandExecutor.h"

#include <algorithm>
#include <sstream>
#include <sys/stat.h>
#include <cerrno>

namespace containercp::access {

// --- Canonical POSIX permission ---
bool valid_acl_perms(const std::string& s) {
    if (s.size() != 3) return false;
    if (s[0] != 'r' && s[0] != '-') return false;
    if (s[1] != 'w' && s[1] != '-') return false;
    if (s[2] != 'x' && s[2] != '-') return false;
    return true;
}

std::string effective_acl(const std::string& named, const std::string& mask) {
    if (mask.empty() || mask == "rwx") return named;
    std::string out = "---";
    for (int i = 0; i < 3; ++i) {
        if (named[i] == mask[i] && named[i] != '-') out[i] = named[i];
    }
    return out;
}

InspectionStatus classify_getfacl_error(int exit_code, const std::string& stderr_out) {
    if (stderr_out.find("Permission denied") != std::string::npos ||
        stderr_out.find("EACCES") != std::string::npos)
        return InspectionStatus::AccessDenied;
    if (stderr_out.find("not supported") != std::string::npos ||
        stderr_out.find("Operation not supported") != std::string::npos)
        return InspectionStatus::AclUnsupported;
    if (stderr_out.find("No such file") != std::string::npos)
        return InspectionStatus::PathMissing;
    if (exit_code == 127 || stderr_out.find("not found") != std::string::npos)
        return InspectionStatus::AclToolMissing;
    return InspectionStatus::AclParseFailed;
}

bool parse_getfacl(const std::string& output, const std::string& target_group,
                   AclState& state, InspectionStatus& status, std::string& error_detail) {
    status = InspectionStatus::Ok;

    if (output.rfind("getfacl:", 0) == 0) {
        status = InspectionStatus::AclParseFailed;
        error_detail = "getfacl error in output";
        return false;
    }

    std::istringstream ss(output);
    std::string line;
    int ln = 0;
    bool ag_seen = false, dg_seen = false, am_seen = false, dm_seen = false;

    while (std::getline(ss, line)) {
        ln++;
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("getfacl:", 0) == 0) {
            status = InspectionStatus::AclParseFailed;
            error_detail = "getfacl error line " + std::to_string(ln);
            return false;
        }
        // access group
        if (line.rfind("group:", 0) == 0 && line.rfind("default:group:", 0) != 0) {
            auto c = line.find(':', 6);
            if (c == std::string::npos) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad group line " + std::to_string(ln); return false; }
            std::string nm = line.substr(6, c - 6);
            std::string pm = line.substr(c + 1);
            if (!valid_acl_perms(pm)) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad perms line " + std::to_string(ln); return false; }
            if (nm == target_group) {
                if (ag_seen) { status = InspectionStatus::MalformedAclOutput; error_detail = "dup group " + std::to_string(ln); return false; }
                ag_seen = true;
                state.access_present = true; state.access_group = nm; state.access_perms = pm;
            }
        // default group
        } else if (line.rfind("default:group:", 0) == 0) {
            auto c = line.find(':', 14);
            if (c == std::string::npos) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad dflt group " + std::to_string(ln); return false; }
            std::string nm = line.substr(14, c - 14);
            std::string pm = line.substr(c + 1);
            if (!valid_acl_perms(pm)) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad dflt perms " + std::to_string(ln); return false; }
            if (nm == target_group) {
                if (dg_seen) { status = InspectionStatus::MalformedAclOutput; error_detail = "dup dflt group " + std::to_string(ln); return false; }
                dg_seen = true;
                state.default_present = true; state.default_group = nm; state.default_perms = pm;
            }
        // access mask
        } else if (line.rfind("mask:", 0) == 0 && line.rfind("default:mask:", 0) != 0) {
            if (am_seen) { status = InspectionStatus::MalformedAclOutput; error_detail = "dup mask " + std::to_string(ln); return false; }
            if (line.size() < 8) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad mask " + std::to_string(ln); return false; }
            std::string m = line.substr(5);
            if (!valid_acl_perms(m)) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad mask p " + std::to_string(ln); return false; }
            am_seen = true; state.access_mask = m;
        // default mask
        } else if (line.rfind("default:mask:", 0) == 0) {
            if (dm_seen) { status = InspectionStatus::MalformedAclOutput; error_detail = "dup dmask " + std::to_string(ln); return false; }
            if (line.size() < 16) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad dmask " + std::to_string(ln); return false; }
            std::string m = line.substr(13);
            if (!valid_acl_perms(m)) { status = InspectionStatus::MalformedAclOutput; error_detail = "bad dmask p " + std::to_string(ln); return false; }
            dm_seen = true; state.default_mask = m;
        // user/other/default:user/default:other — allowed
        } else if (line.rfind("user:", 0) == 0 || line.rfind("other:", 0) == 0 ||
                   line.rfind("default:user:", 0) == 0 || line.rfind("default:other:", 0) == 0) {
            // permitted
        } else {
            status = InspectionStatus::MalformedAclOutput;
            error_detail = "unknown line " + std::to_string(ln) + ": " + line.substr(0, 40);
            return false;
        }
    }
    state.effective_perms = effective_acl(state.access_perms, state.access_mask);
    state.default_effective = effective_acl(state.default_perms, state.default_mask);
    return true;
}

// --- Real implementation ---
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
        if (!s.exists) return s;

        auto result = executor_.run({"getfacl", "-cp", path});
        if (result.exit_code != 0) {
            s.acl_status = classify_getfacl_error(result.exit_code, result.err);
            return s;
        }

        AclState acl;
        InspectionStatus st;
        std::string err;
        if (!parse_getfacl(result.out, groupname, acl, st, err)) {
            s.acl_status = st; s.acl_error_detail = err; return s;
        }
        s.acl = acl; s.acl_status = InspectionStatus::Ok;
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
    auto result = exec.run({"getfacl", "-cp", test_path});
    if (result.exit_code != 0) return classify_getfacl_error(result.exit_code, result.err);
    return InspectionStatus::Ok;
}

} // namespace containercp::access
