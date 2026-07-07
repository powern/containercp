#ifndef CONTAINERCP_BACKUP_BACKUP_H
#define CONTAINERCP_BACKUP_BACKUP_H

#include "core/Resource.h"

#include <cstdint>
#include <string>

namespace containercp::backup {

struct Backup : core::Resource {
    uint64_t site_id = 0;
    uint64_t owner_id = 0;
    std::string filename;
    std::string type = "manual";
    uint64_t size = 0;
    std::string created_at;
    std::string status = "completed";
};

} // namespace containercp::backup

#endif // CONTAINERCP_BACKUP_BACKUP_H
