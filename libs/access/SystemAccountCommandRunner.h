#ifndef CONTAINERCP_ACCESS_SYSTEM_ACCOUNT_COMMAND_RUNNER_H
#define CONTAINERCP_ACCESS_SYSTEM_ACCOUNT_COMMAND_RUNNER_H

#include "core/OperationResult.h"

#include <functional>
#include <string>
#include <vector>

namespace containercp::access {

enum class CommandError {
    None,
    NotAllowed,
    InvalidArg,
    ExecutionFailed,
    TimedOut,
    NonZeroExit,
    Signaled,
    OutputTruncated
};

class SystemAccountCommandRunner {
public:
    struct Range { int min = 0; int max = 0; };

    struct Command {
        std::vector<std::string> args;
    };

    using RunFn = std::function<core::OperationResult(const Command& cmd)>;

    explicit SystemAccountCommandRunner(RunFn run) : run_(std::move(run)) {}

    void set_managed_root(const std::string& root) { managed_root_ = root; }
    void set_uid_range(Range r) { uid_range_ = r; }
    void set_gid_range(Range r) { gid_range_ = r; }

    core::OperationResult groupadd(const std::string& groupname, int gid);
    core::OperationResult useradd(const std::string& username, int uid, int gid,
                                  const std::string& home, const std::string& shell,
                                  const std::string& groupname);
    core::OperationResult usermod_add_group(const std::string& username,
                                            const std::string& groupname);
    core::OperationResult usermod_remove_group(const std::string& username,
                                               const std::string& groupname);
    core::OperationResult passwd_lock(const std::string& username);
    core::OperationResult passwd_unlock(const std::string& username);
    core::OperationResult userdel(const std::string& username);
    core::OperationResult groupdel(const std::string& groupname);
    core::OperationResult usermod_expiredate(const std::string& username, const std::string& date);
    core::OperationResult usermod_shell(const std::string& username, const std::string& shell);

    core::OperationResult chgrp(const std::string& group, const std::string& path);
    core::OperationResult chmod(const std::string& mode, const std::string& path);
    core::OperationResult setfacl_modify(const std::string& acl_spec, const std::string& path);
    core::OperationResult setfacl_remove(const std::string& acl_spec, const std::string& path);

    core::OperationResult mkdir_p(const std::string& path);
    core::OperationResult mount_bind(const std::string& source, const std::string& target);
    core::OperationResult umount(const std::string& target);
    core::OperationResult mountpoint_check(const std::string& path);
    core::OperationResult rmdir(const std::string& path);
    core::OperationResult chown_root(const std::string& path);
    core::OperationResult dir_is_empty(const std::string& path);

    // Resolve canonical absolute path for a command type
    static std::string canonical_path(const std::string& command_type);

    // Verify every approved executable identity before privileged use.
    static core::OperationResult validate_canonical_executable_identities();

private:
    static std::string sanitize_for_log(const std::string& arg);
    static bool contains_control_chars(const std::string& s);
    static bool is_valid_username(const std::string& s);
    static bool is_valid_groupname(const std::string& s);
    static bool is_valid_octal_mode(const std::string& s);
    static bool is_valid_acl_spec(const std::string& s);
    static bool is_valid_shell(const std::string& s);
    static bool is_valid_date(const std::string& s);
    static bool is_valid_perm_chars(const std::string& s);

    core::OperationResult reject(CommandError err, const std::string& msg) const;
    core::OperationResult validate_path_managed(const std::string& path) const;
    core::OperationResult validate_uid(int uid) const;
    core::OperationResult validate_gid(int gid) const;

    std::string managed_root_;
    Range uid_range_{0, 0};
    Range gid_range_{0, 0};
    RunFn run_;
};

} // namespace containercp::access

#endif
