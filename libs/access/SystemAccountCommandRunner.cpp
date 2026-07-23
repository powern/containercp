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

} // namespace containercp::access
