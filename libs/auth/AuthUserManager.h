#ifndef CONTAINERCP_AUTH_AUTH_USER_MANAGER_H
#define CONTAINERCP_AUTH_AUTH_USER_MANAGER_H

#include "auth/AuthUser.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::auth {

class AuthUserManager {
public:
    uint64_t create(const AuthUser& user);
    bool remove(uint64_t id);
    AuthUser* find(const std::string& username);
    AuthUser* find(uint64_t id);
    const std::vector<AuthUser>& list() const;
    void set_users(const std::vector<AuthUser>& users);

private:
    std::vector<AuthUser> users_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::auth

#endif // CONTAINERCP_AUTH_AUTH_USER_MANAGER_H
