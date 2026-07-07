#include "UserManager.h"

namespace containercp::user {

uint64_t UserManager::create(const std::string& username, uint64_t uid, const std::string& home_directory, const std::string& shell) {
    User u;
    u.id = next_id_++;
    u.name = username;
    u.username = username;
    u.uid = uid;
    u.home_directory = home_directory;
    u.shell = shell;
    u.enabled = true;
    users_.push_back(std::move(u));
    return u.id;
}

bool UserManager::remove(uint64_t id) {
    for (auto it = users_.begin(); it != users_.end(); ++it) {
        if (it->id == id) {
            users_.erase(it);
            return true;
        }
    }
    return false;
}

User* UserManager::find(const std::string& username) {
    for (auto& u : users_) {
        if (u.username == username) {
            return &u;
        }
    }
    return nullptr;
}

User* UserManager::find(uint64_t id) {
    for (auto& u : users_) {
        if (u.id == id) {
            return &u;
        }
    }
    return nullptr;
}

const std::vector<User>& UserManager::list() const {
    return users_;
}

void UserManager::set_users(const std::vector<User>& users) {
    users_ = users;
    next_id_ = 1;
    for (const auto& u : users_) {
        if (u.id >= next_id_) {
            next_id_ = u.id + 1;
        }
    }
}

} // namespace containercp::user
