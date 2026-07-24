#ifndef CONTAINERCP_ACCESS_SYSTEM_ACCOUNT_COMMAND_RUNNER_H
#define CONTAINERCP_ACCESS_SYSTEM_ACCOUNT_COMMAND_RUNNER_H

#include "core/OperationResult.h"

#include <functional>
#include <string>
#include <vector>

namespace containercp::access {

// Builds and runs safe, structured privileged host commands.
// No shell, no string interpolation, no std::system().
// Accepts a runnable callback for testability.
class SystemAccountCommandRunner {
public:
    struct Command {
        std::vector<std::string> args;
    };

    using RunFn = std::function<core::OperationResult(const Command& cmd)>;

    explicit SystemAccountCommandRunner(RunFn run) : run_(std::move(run)) {}

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

    // --- Phase 3b: Filesystem permission commands ---
    core::OperationResult chgrp(const std::string& group, const std::string& path);
    core::OperationResult chmod(const std::string& mode, const std::string& path);
    core::OperationResult setfacl_modify(const std::string& acl_spec, const std::string& path);
    core::OperationResult setfacl_remove(const std::string& acl_spec, const std::string& path);

    // --- Phase 3c: Filesystem mount commands ---
    core::OperationResult mkdir_p(const std::string& path);
    core::OperationResult mount_bind(const std::string& source, const std::string& target);
    core::OperationResult umount(const std::string& target);
    core::OperationResult mountpoint_check(const std::string& path);
    core::OperationResult rmdir(const std::string& path);
    core::OperationResult chown_root(const std::string& path);

private:
    RunFn run_;
};

} // namespace containercp::access

#endif
