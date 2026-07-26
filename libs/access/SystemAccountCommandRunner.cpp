#include "access/SystemAccountCommandRunner.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace containercp::access {
namespace {

const std::unordered_set<std::string>& allowed_executables() {
    static const std::unordered_set<std::string> kAllowed = {
        "groupadd", "useradd", "usermod", "passwd", "userdel", "groupdel",
        "gpasswd", "chgrp", "chmod", "setfacl", "mkdir", "mount", "umount",
        "mountpoint", "rmdir", "chown", "ls"
    };
    return kAllowed;
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

bool SystemAccountCommandRunner::is_valid_path(const std::string& s) {
    if (s.empty()) return false;
    if (s[0] != '/') return false;
    if (contains_control_chars(s)) return false;
    if (s.find("..") != std::string::npos) return false;
    return true;
}

bool SystemAccountCommandRunner::is_valid_mode(const std::string& s) {
    if (s.empty() || contains_control_chars(s)) return false;
    if (s.size() >= 3 && s.size() <= 4) {
        bool all_octal = true;
        for (char c : s) {
            if (c < '0' || c > '7') { all_octal = false; break; }
        }
        if (all_octal) return true;
    }
    for (char c : s) {
        if (c == '+' || c == '-' || c == '=') return true;
    }
    return false;
}

bool SystemAccountCommandRunner::is_valid_acl_spec(const std::string& s) {
    if (s.empty() || contains_control_chars(s)) return false;
    return s.find(':') != std::string::npos;
}

bool SystemAccountCommandRunner::is_valid_shell(const std::string& s) {
    if (s.empty() || contains_control_chars(s)) return false;
    return s[0] == '/';
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

bool SystemAccountCommandRunner::is_allowed_executable(const std::string& exe) {
    return allowed_executables().count(exe) > 0;
}

core::OperationResult SystemAccountCommandRunner::reject(CommandError, const std::string& msg) const {
    return {false, msg, ""};
}

core::OperationResult SystemAccountCommandRunner::validate_allowed(const std::string& exe) const {
    if (!is_allowed_executable(exe)) {
        return reject(CommandError::NotAllowed, "not_allowed:" + sanitize_for_log(exe));
    }
    return {true, "", ""};
}

// --- account management ---

core::OperationResult SystemAccountCommandRunner::groupadd(const std::string& groupname, int gid) {
    if (!is_valid_groupname(groupname)) return reject(CommandError::InvalidArg, "invalid_groupname");
    if (gid != -1 && gid <= 0) return reject(CommandError::InvalidArg, "invalid_gid");
    auto vr = validate_allowed("groupadd"); if (!vr.success) return vr;

    if (gid > 0) return run_({{"groupadd", "-g", std::to_string(gid), groupname}});
    return run_({{"groupadd", groupname}});
}

core::OperationResult SystemAccountCommandRunner::useradd(const std::string& username,
                                                           int uid, int gid,
                                                           const std::string& home,
                                                           const std::string& shell,
                                                           const std::string&) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    if (uid <= 0) return reject(CommandError::InvalidArg, "invalid_uid");
    if (gid <= 0) return reject(CommandError::InvalidArg, "invalid_gid");
    if (!is_valid_path(home)) return reject(CommandError::InvalidArg, "invalid_home");
    if (!is_valid_shell(shell)) return reject(CommandError::InvalidArg, "invalid_shell");
    auto vr = validate_allowed("useradd"); if (!vr.success) return vr;

    return run_({{"useradd",
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
    auto vr = validate_allowed("usermod"); if (!vr.success) return vr;

    return run_({{"usermod", "-a", "-G", groupname, username}});
}

core::OperationResult SystemAccountCommandRunner::usermod_remove_group(const std::string& username,
                                                                        const std::string& groupname) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    if (!is_valid_groupname(groupname)) return reject(CommandError::InvalidArg, "invalid_groupname");
    auto vr = validate_allowed("gpasswd"); if (!vr.success) return vr;

    return run_({{"gpasswd", "-d", username, groupname}});
}

core::OperationResult SystemAccountCommandRunner::passwd_lock(const std::string& username) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    auto vr = validate_allowed("passwd"); if (!vr.success) return vr;

    return run_({{"passwd", "-l", username}});
}

core::OperationResult SystemAccountCommandRunner::passwd_unlock(const std::string& username) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    auto vr = validate_allowed("passwd"); if (!vr.success) return vr;

    return run_({{"passwd", "-u", username}});
}

core::OperationResult SystemAccountCommandRunner::userdel(const std::string& username) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    auto vr = validate_allowed("userdel"); if (!vr.success) return vr;

    return run_({{"userdel", username}});
}

core::OperationResult SystemAccountCommandRunner::groupdel(const std::string& groupname) {
    if (!is_valid_groupname(groupname)) return reject(CommandError::InvalidArg, "invalid_groupname");
    auto vr = validate_allowed("groupdel"); if (!vr.success) return vr;

    return run_({{"groupdel", groupname}});
}

core::OperationResult SystemAccountCommandRunner::usermod_expiredate(const std::string& username,
                                                                      const std::string& date) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    if (!is_valid_date(date)) return reject(CommandError::InvalidArg, "invalid_date");
    auto vr = validate_allowed("usermod"); if (!vr.success) return vr;

    return run_({{"usermod", "--expiredate", date, username}});
}

core::OperationResult SystemAccountCommandRunner::usermod_shell(const std::string& username,
                                                                 const std::string& shell) {
    if (!is_valid_username(username)) return reject(CommandError::InvalidArg, "invalid_username");
    if (!is_valid_shell(shell)) return reject(CommandError::InvalidArg, "invalid_shell");
    auto vr = validate_allowed("usermod"); if (!vr.success) return vr;

    return run_({{"usermod", "-s", shell, username}});
}

// --- filesystem permissions ---

core::OperationResult SystemAccountCommandRunner::chgrp(const std::string& group, const std::string& path) {
    if (!is_valid_groupname(group)) return reject(CommandError::InvalidArg, "invalid_group");
    if (!is_valid_path(path)) return reject(CommandError::InvalidArg, "invalid_path");
    auto vr = validate_allowed("chgrp"); if (!vr.success) return vr;

    return run_({{"chgrp", group, "--", path}});
}

core::OperationResult SystemAccountCommandRunner::chmod(const std::string& mode, const std::string& path) {
    if (!is_valid_mode(mode)) return reject(CommandError::InvalidArg, "invalid_mode");
    if (!is_valid_path(path)) return reject(CommandError::InvalidArg, "invalid_path");
    auto vr = validate_allowed("chmod"); if (!vr.success) return vr;

    return run_({{"chmod", mode, "--", path}});
}

core::OperationResult SystemAccountCommandRunner::setfacl_modify(const std::string& acl_spec, const std::string& path) {
    if (!is_valid_acl_spec(acl_spec)) return reject(CommandError::InvalidArg, "invalid_acl_spec");
    if (!is_valid_path(path)) return reject(CommandError::InvalidArg, "invalid_path");
    auto vr = validate_allowed("setfacl"); if (!vr.success) return vr;

    return run_({{"setfacl", "-m", acl_spec, "--", path}});
}

core::OperationResult SystemAccountCommandRunner::setfacl_remove(const std::string& acl_spec, const std::string& path) {
    if (!is_valid_acl_spec(acl_spec)) return reject(CommandError::InvalidArg, "invalid_acl_spec");
    if (!is_valid_path(path)) return reject(CommandError::InvalidArg, "invalid_path");
    auto vr = validate_allowed("setfacl"); if (!vr.success) return vr;

    return run_({{"setfacl", "-x", acl_spec, "--", path}});
}

// --- mount commands ---

core::OperationResult SystemAccountCommandRunner::mkdir_p(const std::string& path) {
    if (!is_valid_path(path)) return reject(CommandError::InvalidArg, "invalid_path");
    auto vr = validate_allowed("mkdir"); if (!vr.success) return vr;

    return run_({{"mkdir", "-p", "--", path}});
}

core::OperationResult SystemAccountCommandRunner::mount_bind(const std::string& source, const std::string& target) {
    if (!is_valid_path(source)) return reject(CommandError::InvalidArg, "invalid_source");
    if (!is_valid_path(target)) return reject(CommandError::InvalidArg, "invalid_target");
    auto vr = validate_allowed("mount"); if (!vr.success) return vr;

    return run_({{"mount", "--bind", "--", source, target}});
}

core::OperationResult SystemAccountCommandRunner::umount(const std::string& target) {
    if (!is_valid_path(target)) return reject(CommandError::InvalidArg, "invalid_target");
    auto vr = validate_allowed("umount"); if (!vr.success) return vr;

    return run_({{"umount", "--", target}});
}

core::OperationResult SystemAccountCommandRunner::mountpoint_check(const std::string& path) {
    if (!is_valid_path(path)) return reject(CommandError::InvalidArg, "invalid_path");
    auto vr = validate_allowed("mountpoint"); if (!vr.success) return vr;

    return run_({{"mountpoint", "-q", "--", path}});
}

core::OperationResult SystemAccountCommandRunner::rmdir(const std::string& path) {
    if (!is_valid_path(path)) return reject(CommandError::InvalidArg, "invalid_path");
    auto vr = validate_allowed("rmdir"); if (!vr.success) return vr;

    return run_({{"rmdir", "--", path}});
}

core::OperationResult SystemAccountCommandRunner::chown_root(const std::string& path) {
    if (!is_valid_path(path)) return reject(CommandError::InvalidArg, "invalid_path");
    auto vr = validate_allowed("chown"); if (!vr.success) return vr;

    return run_({{"chown", "root:root", "--", path}});
}

core::OperationResult SystemAccountCommandRunner::dir_is_empty(const std::string& path) {
    if (!is_valid_path(path)) return reject(CommandError::InvalidArg, "invalid_path");
    auto vr = validate_allowed("ls"); if (!vr.success) return vr;

    auto result = run_({{"ls", "-A", "--", path}});
    result.success = result.output.empty();
    if (!result.success) result.message = "directory not empty";
    return result;
}

} // namespace containercp::access
