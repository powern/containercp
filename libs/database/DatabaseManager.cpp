#include "DatabaseManager.h"

namespace containercp::database {

uint64_t DatabaseManager::create(const std::string& db_name, const std::string& db_user, const std::string& db_password, uint64_t owner_id, uint64_t site_id) {
    Database d;
    d.id = next_id_++;
    d.name = db_name;
    d.db_name = db_name;
    d.db_user = db_user;
    d.db_password = db_password;
    d.engine = "mariadb";
    d.version = "lts";
    d.owner_id = owner_id;
    d.site_id = site_id;
    d.enabled = true;
    databases_.push_back(std::move(d));
    return d.id;
}

bool DatabaseManager::remove(uint64_t id) {
    for (auto it = databases_.begin(); it != databases_.end(); ++it) {
        if (it->id == id) {
            databases_.erase(it);
            return true;
        }
    }
    return false;
}

Database* DatabaseManager::find(uint64_t id) {
    for (auto& d : databases_) {
        if (d.id == id) {
            return &d;
        }
    }
    return nullptr;
}

Database* DatabaseManager::find(const std::string& db_name) {
    for (auto& d : databases_) {
        if (d.db_name == db_name) {
            return &d;
        }
    }
    return nullptr;
}

const std::vector<Database>& DatabaseManager::list() const {
    return databases_;
}

void DatabaseManager::set_databases(const std::vector<Database>& databases) {
    databases_ = databases;
    next_id_ = 1;
    for (const auto& d : databases_) {
        if (d.id >= next_id_) {
            next_id_ = d.id + 1;
        }
    }
}

} // namespace containercp::database
