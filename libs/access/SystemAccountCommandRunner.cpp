#include "access/ManagedPathValidator.h"
#include "access/SystemAccountCommandRunner.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace containercp::access {
namespace {

const std::map<std::string, std::string>& canonical_paths() {
    static const std::map<std::string, std::string> kPaths = {
        {"groupadd", "/usr/sbin/groupadd"},
        {"useradd", "/usr/sbin/useradd"},
        {"usermod", "/usr/sbin/usermod"},
        {"userdel", "/usr/sbin/userdel"},
        {"groupdel", "/usr/sbin/groupdel"},
        {"passwd", "/usr/bin/passwd"},
        {"gpasswd", "/usr/bin/gpasswd"},
        {"chgrp", "/usr/bin/chgrp"},
        {"chmod", "/usr/bin/chmod"},
        {"setfacl", "/usr/bin/setfacl"},
        {"mkdir", "/usr/bin/mkdir"},
        {"mount", "/usr/bin/mount"},
        {"umount", "/usr/bin/umount"},
        {"mountpoint", "/usr/bin/mountpoint"},
        {"rmdir", "/usr/bin/rmdir"},
        {"chown", "/usr/bin/chown"},
        {"ls", "/usr/bin/ls"},
    };
    return kPaths;
}

bool has_any_dotdot_component(const std::string& s) {
    std::string::size_type pos = 0;
    while (pos < s.size()) {
        auto slash = s.find('/', pos);
        std::string comp = (slash == std::string::npos) ? s.substr(pos) : s.substr(pos, slash - pos);
        if (comp == "..") return true;
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }
    return false;
}

} // namespace

std::string SystemAccountCommandRunner::sanitize_for_log(const std::string& arg) {
    std::string out;
    out.reserve(arg.size());
    for (unsigned char c : arg) {
        if (c < 0x20 || c == 0x7f) {
            out += '?';
        } else {
            out += static_cast<char>(c);
        }
    }
    return out;
}

bool SystemAccountCommandRunner::contains_control_chars(const std::string& s) {
    for (unsigned char c : s) {
        if (c < 0x20 && c != '\t') return true;
        if (c == 0x7f) return true;
    }
    return false;
}

bool SystemAccountCommandRunner::is_valid_username(const std::string& s) {
    if (s.empty() || s.size() > 32) return false;
    if (contains_control_chars(s)) return false;
    for (char c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') return false;
    }
    return true;
}

bool SystemAccountCommandRunner::is_valid_groupname(const std::string& s) {
    return is_valid_username(s);
}

bool SystemAccountCommandRunner::is_valid_octal_mode(const std::string& s) {
    if (s.empty() || s.size() < 3 || s.size() > 4) return false;
    if (contains_control_chars(s)) return false;
    for (char c : s) {
        if (c < '0' || c > '7') return false;
    }
    return true;
}

bool SystemAccountCommandRunner::is_valid_perm_chars(const std::string& s) {
    if (s.empty() || s.size() > 3) return false;
    for (char c : s) {
        if (c != 'r' && c != 'w' && c != 'x' && c != '-') return false;
    }
    return true;
}

bool SystemAccountCommandRunner::is_valid_acl_spec(const std::string& s) {
    if (s.empty() || contains_control_chars(s)) return false;
    if (s.size() > 256) return false;

    // Access group entry: g:{group}:{perms}
    if (s.rfind("g:", 0) == 0) {
        auto rest = s.substr(2);
        auto colon = rest.find(':');
        if (colon == std::string::npos) {
            // g:{group} — removal form
            return is_valid_groupname(rest);
        }
        std::string group = rest.substr(0, colon);
        std::string perms = rest.substr(colon + 1);
        return is_valid_groupname(group) && is_valid_perm_chars(perms);
    }

    // Default group entry: d:g:{group}:{perms}
    if (s.rfind("d:g:", 0) == 0) {
        auto rest = s.substr(4);
        auto colon = rest.find(':');
        if (colon == std::string::npos) {
            // d:g:{group} — removal form
            return is_valid_groupname(rest);
        }
        std::string group = rest.substr(0, colon);
        std::string perms = rest.substr(colon + 1);
        return is_valid_groupname(group) && is_valid_perm_chars(perms);
    }

    return false;
}

bool SystemAccountCommandRunner::is_valid_shell(const std::string& s) {
    if (s.empty() || contains_control_chars(s)) return false;
    if (s[0] != '/') return false;
    if (s.find("..") != std::string::npos) return false;
    if (s.size() > 256) return false;
    return true;
}

bool SystemAccountCommandRunner::is_valid_date(const std::string& s) {
    if (contains_control_chars(s)) return false;
    if (s.empty() || s == "never" || s == "-1") return true;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (i == 4 || i == 7) {
            if (c != '-') return false;
        } else if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

std::string SystemAccountCommandRunner::canonical_path(const std::string& command_type) {
    auto it = canonical_paths().find(command_type);
    if (it == canonical_paths().end()) return {};
    return it->second;
}

core::OperationResult SystemAccountCommandRunner::validate_canonical_executable_identities() {
    std::ostringstream errors;
    bool ok = true;
    for (const auto& [type, path] : canonical_paths()) {
        struct stat st{};
        if (::lstat(path.c_str(), &st) != 0) {
            ok = false;
            errors << type << ":missing:" << path << ";";
            continue;
        }
        if (S_ISLNK(st.st_mode)) {
            ok = false;
            errors << type << ":symlink:" << path << ";";
            continue;
        }
        if (!S_ISREG(st.st_mode)) {
            ok = false;
            errors << type << ":not_regular:" << path << ";";
            continue;
        }
        if (st.st_uid != 0) {
            ok = false;
            errors << type << ":not_root_owned:" << path << ";";
        }
        if ((st.st_mode & S_IXUSR) == 0) {
            ok = false;
            errors << type << ":not_executable:" << path << ";";
        }
        if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
            ok = false;
            errors << type << ":writable_by_group_or_world:" << path << ";";
        }
    }
    if (!ok) return {false, errors.str(), ""};
    return {true, "canonical executable identities verified", ""};
}

core::OperationResult SystemAccountCommandRunner::reject(CommandError, const std::string& msg) const {
    return {false, msg, ""};
}

core::OperationResult SystemAccountCommandRunner::validate_path_managed(const std::string& path) const {
    if (path.empty()) return reject(CommandError::InvalidArg, "empty_path");
    if (path[0] != '/') return reject(CommandError::InvalidArg, "relative_path");
    if (contains_control_chars(path)) return reject(CommandError::InvalidArg, "control_chars");
    if (has_any_dotdot_component(path)) return reject(CommandError::InvalidArg, "dotdot_component");
    if (!managed_root_.empty()) {
        auto pv = validate_managed_path(path, managed_root_);
        if (!pv.ok) return reject(CommandError::InvalidArg, "managed_path:" + pv.error);
    }
    return {true, "", ""};
}

core::OperationResult SystemAccountCommandRunner::validate_uid(int uid) const {
    if (uid <= 0) return reject(CommandError::InvalidArg, "invalid_uid");
    if (uid_range_.min > 0 && uid < uid_range_.min) return reject(CommandError::InvalidArg, "uid_below_range");
    if (uid_range_.max > 0 && uid > uid_range_.max) return reject(CommandError::InvalidArg, "uid_above_range");
    return {true, "", ""};
}

core::OperationResult SystemAccountCommandRunner::validate_gid(int gid) const {
    if (gid <= 0) return reject(CommandError::InvalidArg, "invalid_gid");
    if (gid_range_.min > 0 && gid < gid_range_.min) return reject(CommandError::InvalidArg, "gid_below_range");
    if (gid_range_.max > 0 && gid > gid_range_.max) return reject(CommandError::InvalidArg, "gid_above_range");
    return {true, "", ""};
}

// --- account management ---

core::OperationResult SystemAccountCommandRunner::groupadd(const std::string& groupname, int gid) {
    if (!is_valid_groupname(groupname)) return reject(CommandError::InvalidArg, "invalid_groupname");
    if (gid != -1) {
        auto vr = validate_gid(gid); if (!vr.success) return vr;
    }
    std::string exe = canonical_path("groupadd");
    if (exe.empty()) return reject(CommandError::NotAllowed, "groupadd");

    if (gid > 0) return run_({{exe, "-g", std::to_string(gid), groupname}});
    return run_({{exe, groupname}});
}

core::OperationResult SystemAccountCommandRunner::useradd(const std::string& username,
                                                           int uid, int gid,
                                                           const std::string& home,
                                                           const std::string& shell,
                                                           const std::string&) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    { auto vr = validate_uid(uid); if (!vr.success) return vr; }
    { auto vr = validate_gid(gid); if (!vr.success) return vr; }
    { auto vr = validate_path_managed(home); if (!vr.success) return vr; }
    if (!is_valid_shell(shell)) return reject(CommandError::InvalidArg, "invalid_shell");
    std::string exe = canonical_path("useradd");
    if (exe.empty()) return reject(CommandError::NotAllowed, "useradd");

    return run_({{exe,
                  "-u", std::to_string(uid),
                  "-g", std::to_string(gid),
                  "-d", home,
                  "-s", shell,
                  "-M",
                  username}});
}

core::OperationResult SystemAccountCommandRunner::usermod_add_group(const std::string& username,
                                                                     const std::string& groupname) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    if (!is_valid_groupname(groupname)) return reject(CommandError::InvalidArg, "invalid_groupname");
    std::string exe = canonical_path("usermod");
    if (exe.empty()) return reject(CommandError::NotAllowed, "usermod");

    return run_({{exe, "-a", "-G", groupname, username}});
}

core::OperationResult SystemAccountCommandRunner::usermod_remove_group(const std::string& username,
                                                                        const std::string& groupname) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    if (!is_valid_groupname(groupname)) return reject(CommandError::InvalidArg, "invalid_groupname");
    std::string exe = canonical_path("gpasswd");
    if (exe.empty()) return reject(CommandError::NotAllowed, "gpasswd");

    return run_({{exe, "-d", username, groupname}});
}

core::OperationResult SystemAccountCommandRunner::passwd_lock(const std::string& username) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    std::string exe = canonical_path("passwd");
    if (exe.empty()) return reject(CommandError::NotAllowed, "passwd");

    return run_({{exe, "-l", username}});
}

core::OperationResult SystemAccountCommandRunner::passwd_unlock(const std::string& username) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    std::string exe = canonical_path("passwd");
    if (exe.empty()) return reject(CommandError::NotAllowed, "passwd");

    return run_({{exe, "-u", username}});
}

core::OperationResult SystemAccountCommandRunner::userdel(const std::string& username) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    std::string exe = canonical_path("userdel");
    if (exe.empty()) return reject(CommandError::NotAllowed, "userdel");

    return run_({{exe, username}});
}

core::OperationResult SystemAccountCommandRunner::groupdel(const std::string& groupname) {
    if (!is_valid_groupname(groupname)) return reject(CommandError::InvalidArg, "invalid_groupname");
    std::string exe = canonical_path("groupdel");
    if (exe.empty()) return reject(CommandError::NotAllowed, "groupdel");

    return run_({{exe, groupname}});
}

core::OperationResult SystemAccountCommandRunner::usermod_expiredate(const std::string& username,
                                                                      const std::string& date) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    if (!is_valid_date(date)) return reject(CommandError::InvalidArg, "invalid_date");
    std::string exe = canonical_path("usermod");
    if (exe.empty()) return reject(CommandError::NotAllowed, "usermod");

    return run_({{exe, "--expiredate", date, username}});
}

core::OperationResult SystemAccountCommandRunner::usermod_shell(const std::string& username,
                                                                 const std::string& shell) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    if (!is_valid_shell(shell)) return reject(CommandError::InvalidArg, "invalid_shell");
    std::string exe = canonical_path("usermod");
    if (exe.empty()) return reject(CommandError::NotAllowed, "usermod");

    return run_({{exe, "-s", shell, username}});
}

// --- filesystem permissions ---

core::OperationResult SystemAccountCommandRunner::chgrp(const std::string& group, const std::string& path) {
    if (!is_valid_groupname(group)) return reject(CommandError::InvalidArg, "invalid_group");
    { auto vr = validate_path_managed(path); if (!vr.success) return vr; }
    std::string exe = canonical_path("chgrp");
    if (exe.empty()) return reject(CommandError::NotAllowed, "chgrp");

    return run_({{exe, group, "--", path}});
}

core::OperationResult SystemAccountCommandRunner::chmod(const std::string& mode, const std::string& path) {
    if (!is_valid_octal_mode(mode)) return reject(CommandError::InvalidArg, "invalid_mode");
    { auto vr = validate_path_managed(path); if (!vr.success) return vr; }
    std::string exe = canonical_path("chmod");
    if (exe.empty()) return reject(CommandError::NotAllowed, "chmod");

    return run_({{exe, mode, "--", path}});
}

core::OperationResult SystemAccountCommandRunner::setfacl_modify(const std::string& acl_spec, const std::string& path) {
    if (!is_valid_acl_spec(acl_spec)) return reject(CommandError::InvalidArg, "invalid_acl_spec");
    { auto vr = validate_path_managed(path); if (!vr.success) return vr; }
    std::string exe = canonical_path("setfacl");
    if (exe.empty()) return reject(CommandError::NotAllowed, "setfacl");

    return run_({{exe, "-m", acl_spec, "--", path}});
}

core::OperationResult SystemAccountCommandRunner::setfacl_remove(const std::string& acl_spec, const std::string& path) {
    if (!is_valid_acl_spec(acl_spec)) return reject(CommandError::InvalidArg, "invalid_acl_spec");
    { auto vr = validate_path_managed(path); if (!vr.success) return vr; }
    std::string exe = canonical_path("setfacl");
    if (exe.empty()) return reject(CommandError::NotAllowed, "setfacl");

    return run_({{exe, "-x", acl_spec, "--", path}});
}

// --- mount commands ---

core::OperationResult SystemAccountCommandRunner::mkdir_p(const std::string& path) {
    { auto vr = validate_path_managed(path); if (!vr.success) return vr; }
    std::string exe = canonical_path("mkdir");
    if (exe.empty()) return reject(CommandError::NotAllowed, "mkdir");

    return run_({{exe, "-p", "--", path}});
}

core::OperationResult SystemAccountCommandRunner::mount_bind(const std::string& source, const std::string& target) {
    { auto vr = validate_path_managed(source); if (!vr.success) return vr; }
    { auto vr = validate_path_managed(target); if (!vr.success) return vr; }
    std::string exe = canonical_path("mount");
    if (exe.empty()) return reject(CommandError::NotAllowed, "mount");

    return run_({{exe, "--bind", "--", source, target}});
}

core::OperationResult SystemAccountCommandRunner::umount(const std::string& target) {
    { auto vr = validate_path_managed(target); if (!vr.success) return vr; }
    std::string exe = canonical_path("umount");
    if (exe.empty()) return reject(CommandError::NotAllowed, "umount");

    return run_({{exe, "--", target}});
}

core::OperationResult SystemAccountCommandRunner::mountpoint_check(const std::string& path) {
    { auto vr = validate_path_managed(path); if (!vr.success) return vr; }
    std::string exe = canonical_path("mountpoint");
    if (exe.empty()) return reject(CommandError::NotAllowed, "mountpoint");

    return run_({{exe, "-q", "--", path}});
}

core::OperationResult SystemAccountCommandRunner::rmdir(const std::string& path) {
    { auto vr = validate_path_managed(path); if (!vr.success) return vr; }
    std::string exe = canonical_path("rmdir");
    if (exe.empty()) return reject(CommandError::NotAllowed, "rmdir");

    return run_({{exe, "--", path}});
}

core::OperationResult SystemAccountCommandRunner::chown_root(const std::string& path) {
    { auto vr = validate_path_managed(path); if (!vr.success) return vr; }
    std::string exe = canonical_path("chown");
    if (exe.empty()) return reject(CommandError::NotAllowed, "chown");

    return run_({{exe, "root:root", "--", path}});
}

core::OperationResult SystemAccountCommandRunner::dir_is_empty(const std::string& path) {
    { auto vr = validate_path_managed(path); if (!vr.success) return vr; }
    std::string exe = canonical_path("ls");
    if (exe.empty()) return reject(CommandError::NotAllowed, "ls");

    auto result = run_({{exe, "-A", "--", path}});
    result.success = result.output.empty();
    if (!result.success) result.message = "directory not empty";
    return result;
}

} // namespace containercp::access
