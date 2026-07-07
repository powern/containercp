#ifndef CONTAINERCP_ACCESS_ACCESS_USER_MANAGER_H
#define CONTAINERCP_ACCESS_ACCESS_USER_MANAGER_H

#include "access/AccessUser.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::access {

class AccessUserManager {
public:
    uint64_t create(const std::string& username);
    bool remove(uint64_t id);
    AccessUser* find(const std::string& username);
    AccessUser* find(uint64_t id);
    const std::vector<AccessUser>& list() const;

    void set_users(const std::vector<AccessUser>& users);

private:
    std::vector<AccessUser> users_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::access

#endif // CONTAINERCP_ACCESS_ACCESS_USER_MANAGER_H
