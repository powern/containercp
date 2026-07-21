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
    std::string file_path;
    std::string compression = "gzip";
    std::string manifest_version = "legacy_unknown";
    std::string backup_completeness = "legacy_unknown";
    bool contains_database = false;
    std::string database_status = "legacy_unknown";
    std::string database_engine;
    std::string database_name;
    uint64_t database_dump_size = 0;
    std::string database_dump_checksum;
    std::string restore_capability = "files_only";
    std::string warning_codes;
};

} // namespace containercp::backup

#endif // CONTAINERCP_BACKUP_BACKUP_H
