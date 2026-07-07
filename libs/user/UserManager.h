#ifndef CONTAINERCP_USER_USER_MANAGER_H
#define CONTAINERCP_USER_USER_MANAGER_H

#include "user/User.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::user {

class UserManager {
public:
    uint64_t create(const std::string& username, uint64_t uid, const std::string& home_directory, const std::string& shell);
    bool remove(uint64_t id);
    User* find(const std::string& username);
    User* find(uint64_t id);
    const std::vector<User>& list() const;

    void set_users(const std::vector<User>& users);

private:
    std::vector<User> users_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::user

#endif // CONTAINERCP_USER_USER_MANAGER_H
