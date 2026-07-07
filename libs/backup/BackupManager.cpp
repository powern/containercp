#include "BackupManager.h"

namespace containercp::backup {

uint64_t BackupManager::create(uint64_t site_id, uint64_t owner_id, const std::string& filename, uint64_t size, const std::string& created_at) {
    Backup b;
    b.id = next_id_++;
    b.name = filename;
    b.site_id = site_id;
    b.owner_id = owner_id;
    b.filename = filename;
    b.type = "manual";
    b.size = size;
    b.created_at = created_at;
    b.status = "completed";
    backups_.push_back(std::move(b));
    return b.id;
}

bool BackupManager::remove(uint64_t id) {
    for (auto it = backups_.begin(); it != backups_.end(); ++it) {
        if (it->id == id) {
            backups_.erase(it);
            return true;
        }
    }
    return false;
}

Backup* BackupManager::find(uint64_t id) {
    for (auto& b : backups_) {
        if (b.id == id) {
            return &b;
        }
    }
    return nullptr;
}

const std::vector<Backup>& BackupManager::list() const {
    return backups_;
}

void BackupManager::set_backups(const std::vector<Backup>& backups) {
    backups_ = backups;
    next_id_ = 1;
    for (const auto& b : backups_) {
        if (b.id >= next_id_) {
            next_id_ = b.id + 1;
        }
    }
}

} // namespace containercp::backup
