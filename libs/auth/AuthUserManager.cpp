#include "AuthUserManager.h"

namespace containercp::auth {

uint64_t AuthUserManager::create(const AuthUser& user) {
    AuthUser u = user;
    if (u.id == 0) {
        u.id = next_id_++;
    } else {
        if (u.id >= next_id_) {
            next_id_ = u.id + 1;
        }
    }
    u.name = u.username;
    users_.push_back(std::move(u));
    return u.id;
}

bool AuthUserManager::remove(uint64_t id) {
    for (auto it = users_.begin(); it != users_.end(); ++it) {
        if (it->id == id) {
            users_.erase(it);
            return true;
        }
    }
    return false;
}

AuthUser* AuthUserManager::find(const std::string& username) {
    for (auto& u : users_) {
        if (u.username == username) {
            return &u;
        }
    }
    return nullptr;
}

AuthUser* AuthUserManager::find(uint64_t id) {
    for (auto& u : users_) {
        if (u.id == id) {
            return &u;
        }
    }
    return nullptr;
}

const std::vector<AuthUser>& AuthUserManager::list() const {
    return users_;
}

void AuthUserManager::set_users(const std::vector<AuthUser>& users) {
    users_ = users;
    next_id_ = 1;
    for (const auto& u : users_) {
        if (u.id >= next_id_) {
            next_id_ = u.id + 1;
        }
    }
}

} // namespace containercp::auth
