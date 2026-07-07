#include "AccessUserManager.h"

namespace containercp::access {

uint64_t AccessUserManager::create(const std::string& username, uint64_t site_id, const std::string& domain) {
    AccessUser u;
    u.id = next_id_++;
    u.name = username;
    u.username = username;
    u.site_id = site_id;
    u.domain = domain;
    u.auth_type = "password";
    u.enabled = true;
    users_.push_back(std::move(u));
    return u.id;
}

bool AccessUserManager::remove(uint64_t id) {
    for (auto it = users_.begin(); it != users_.end(); ++it) {
        if (it->id == id) {
            users_.erase(it);
            return true;
        }
    }
    return false;
}

AccessUser* AccessUserManager::find(const std::string& username) {
    for (auto& u : users_) {
        if (u.username == username) {
            return &u;
        }
    }
    return nullptr;
}

AccessUser* AccessUserManager::find(uint64_t id) {
    for (auto& u : users_) {
        if (u.id == id) {
            return &u;
        }
    }
    return nullptr;
}

const std::vector<AccessUser>& AccessUserManager::list() const {
    return users_;
}

void AccessUserManager::set_users(const std::vector<AccessUser>& users) {
    users_ = users;
    next_id_ = 1;
    for (const auto& u : users_) {
        if (u.id >= next_id_) {
            next_id_ = u.id + 1;
        }
    }
}

} // namespace containercp::access
