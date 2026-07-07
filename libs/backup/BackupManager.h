#ifndef CONTAINERCP_BACKUP_BACKUP_MANAGER_H
#define CONTAINERCP_BACKUP_BACKUP_MANAGER_H

#include "backup/Backup.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::backup {

class BackupManager {
public:
    uint64_t create(uint64_t site_id, uint64_t owner_id, const std::string& filename, uint64_t size, const std::string& created_at);
    bool remove(uint64_t id);
    Backup* find(uint64_t id);
    const std::vector<Backup>& list() const;

    void set_backups(const std::vector<Backup>& backups);

private:
    std::vector<Backup> backups_;
    uint64_t next_id_ = 1;
};

} // namespace containercp::backup

#endif // CONTAINERCP_BACKUP_BACKUP_MANAGER_H
