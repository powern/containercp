#ifndef CONTAINERCP_ACCESS_SYSTEM_IDENTITY_INSPECTOR_H
#define CONTAINERCP_ACCESS_SYSTEM_IDENTITY_INSPECTOR_H

#include <string>
#include <vector>

namespace containercp::access {

struct ObservedUser {
    bool exists = false;
    std::string username;
    int uid = -1;
    int gid = -1;
    std::string home;
    std::string shell;
    bool locked = false;
};

struct ObservedGroup {
    bool exists = false;
    std::string name;
    int gid = -1;
};

// Testable abstraction for inspecting Linux users and groups.
// Default implementation uses getpwnam/getpwuid/getgrnam/getgrgid.
class SystemIdentityInspector {
public:
    virtual ~SystemIdentityInspector() = default;

    virtual ObservedUser  lookup_user(const std::string& name) const = 0;
    virtual ObservedUser  lookup_uid(int uid) const = 0;
    virtual ObservedGroup lookup_group(const std::string& name) const = 0;
    virtual bool          user_exists(const std::string& name) const = 0;
    virtual bool          group_exists(const std::string& name) const = 0;
    virtual bool          uid_occupied(int uid) const = 0;
    virtual bool          gid_occupied(int gid) const = 0;
    virtual bool          user_in_group(const std::string& username,
                                        const std::string& groupname) const = 0;
};

// Real implementation using libc NSS APIs.
class RealSystemIdentityInspector : public SystemIdentityInspector {
public:
    ObservedUser  lookup_user(const std::string& name) const override;
    ObservedUser  lookup_uid(int uid) const override;
    ObservedGroup lookup_group(const std::string& name) const override;
    bool          user_exists(const std::string& name) const override;
    bool          group_exists(const std::string& name) const override;
    bool          uid_occupied(int uid) const override;
    bool          gid_occupied(int gid) const override;
    bool          user_in_group(const std::string& username,
                                const std::string& groupname) const override;
};

} // namespace containercp::access

#endif
