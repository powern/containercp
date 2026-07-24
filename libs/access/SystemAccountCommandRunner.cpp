#include "access/SystemAccountCommandRunner.h"

#include <sstream>

namespace containercp::access {

core::OperationResult SystemAccountCommandRunner::groupadd(const std::string& groupname, int gid) {
    if (gid > 0) return run_({{"groupadd", "-g", std::to_string(gid), groupname}});
    return run_({{"groupadd", groupname}});
}

core::OperationResult SystemAccountCommandRunner::useradd(const std::string& username,
                                                          int uid, int gid,
                                                          const std::string& home,
                                                          const std::string& shell,
                                                          const std::string& groupname) {
    return run_({{"useradd",
                  "-u", std::to_string(uid),
                  "-g", std::to_string(gid),
                  "-d", home,
                  "-s", shell,
                  "-M",                    // do not create home via /etc/skel
                  username}});
}

core::OperationResult SystemAccountCommandRunner::usermod_add_group(const std::string& username,
                                                                     const std::string& groupname) {
    return run_({{"usermod", "-a", "-G", groupname, username}});
}

core::OperationResult SystemAccountCommandRunner::usermod_remove_group(const std::string& username,
                                                                        const std::string& groupname) {
    return run_({{"gpasswd", "-d", username, groupname}});
}

core::OperationResult SystemAccountCommandRunner::passwd_lock(const std::string& username) {
    return run_({{"passwd", "-l", username}});
}

core::OperationResult SystemAccountCommandRunner::passwd_unlock(const std::string& username) {
    return run_({{"passwd", "-u", username}});
}

core::OperationResult SystemAccountCommandRunner::userdel(const std::string& username) {
    return run_({{"userdel", username}});
}

core::OperationResult SystemAccountCommandRunner::groupdel(const std::string& groupname) {
    return run_({{"groupdel", groupname}});
}

core::OperationResult SystemAccountCommandRunner::usermod_expiredate(const std::string& username,
                                                                      const std::string& date) {
    return run_({{"usermod", "--expiredate", date, username}});
}

core::OperationResult SystemAccountCommandRunner::usermod_shell(const std::string& username,
                                                                 const std::string& shell) {
    return run_({{"usermod", "-s", shell, username}});
}

core::OperationResult SystemAccountCommandRunner::chgrp(const std::string& group, const std::string& path) {
    return run_({{"chgrp", group, path}});
}

core::OperationResult SystemAccountCommandRunner::chmod(const std::string& mode, const std::string& path) {
    return run_({{"chmod", mode, path}});
}

core::OperationResult SystemAccountCommandRunner::setfacl_modify(const std::string& acl_spec, const std::string& path) {
    return run_({{"setfacl", "-m", acl_spec, path}});
}

core::OperationResult SystemAccountCommandRunner::setfacl_remove(const std::string& acl_spec, const std::string& path) {
    return run_({{"setfacl", "-x", acl_spec, path}});
}

core::OperationResult SystemAccountCommandRunner::mkdir_p(const std::string& path) {
    return run_({{"mkdir", "-p", path}});
}

core::OperationResult SystemAccountCommandRunner::mount_bind(const std::string& source, const std::string& target) {
    return run_({{"mount", "--bind", source, target}});
}

core::OperationResult SystemAccountCommandRunner::umount(const std::string& target) {
    return run_({{"umount", target}});
}

core::OperationResult SystemAccountCommandRunner::mountpoint_check(const std::string& path) {
    return run_({{"mountpoint", "-q", path}});
}

core::OperationResult SystemAccountCommandRunner::rmdir(const std::string& path) {
    return run_({{"rmdir", path}});
}
} // namespace containercp::access
