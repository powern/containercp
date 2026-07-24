#include "access/SystemIdentityInspector.h"

#include <grp.h>
#include <pwd.h>
#include <shadow.h>
#include <cstring>
#include <sys/types.h>

namespace containercp::access {

ObservedUser RealSystemIdentityInspector::lookup_user(const std::string& name) const {
    ObservedUser u;
    struct passwd* pw = getpwnam(name.c_str());
    if (pw == nullptr) return u;
    u.exists = true;
    u.username = pw->pw_name;
    u.uid = static_cast<int>(pw->pw_uid);
    u.gid = static_cast<int>(pw->pw_gid);
    u.home = pw->pw_dir ? pw->pw_dir : "";
    u.shell = pw->pw_shell ? pw->pw_shell : "";
    struct spwd* sp = getspnam(name.c_str());
    if (sp != nullptr) {
        u.locked = (sp->sp_pwdp != nullptr && sp->sp_pwdp[0] == '!');
    }
    return u;
}

ObservedUser RealSystemIdentityInspector::lookup_uid(int uid) const {
    ObservedUser u;
    struct passwd* pw = getpwuid(static_cast<uid_t>(uid));
    if (pw == nullptr) return u;
    u.exists = true;
    u.username = pw->pw_name;
    u.uid = static_cast<int>(pw->pw_uid);
    u.gid = static_cast<int>(pw->pw_gid);
    u.home = pw->pw_dir ? pw->pw_dir : "";
    u.shell = pw->pw_shell ? pw->pw_shell : "";
    return u;
}

ObservedGroup RealSystemIdentityInspector::lookup_group(const std::string& name) const {
    ObservedGroup g;
    struct group* gr = getgrnam(name.c_str());
    if (gr == nullptr) return g;
    g.exists = true;
    g.name = gr->gr_name;
    g.gid = static_cast<int>(gr->gr_gid);
    return g;
}

bool RealSystemIdentityInspector::user_exists(const std::string& name) const {
    return getpwnam(name.c_str()) != nullptr;
}

bool RealSystemIdentityInspector::group_exists(const std::string& name) const {
    return getgrnam(name.c_str()) != nullptr;
}

bool RealSystemIdentityInspector::uid_occupied(int uid) const {
    return getpwuid(static_cast<uid_t>(uid)) != nullptr;
}

bool RealSystemIdentityInspector::gid_occupied(int gid) const {
    return getgrgid(static_cast<gid_t>(gid)) != nullptr;
}

bool RealSystemIdentityInspector::user_in_group(const std::string& username,
                                                 const std::string& groupname) const {
    struct group* gr = getgrnam(groupname.c_str());
    if (gr == nullptr) return false;
    for (char** mem = gr->gr_mem; *mem != nullptr; ++mem) {
        if (username == *mem) return true;
    }
    return false;
}

} // namespace containercp::access
