#ifndef CONTAINERCP_STORAGE_MIGRATION_ORCHESTRATOR_H
#define CONTAINERCP_STORAGE_MIGRATION_ORCHESTRATOR_H

#include "ConnectionPool.h"
#include "LegacyArchive.h"
#include "LegacyFileInventory.h"
#include "LegacyImporter.h"
#include "Verification.h"

#include <string>

namespace containercp::storage {

struct MigrationResult {
    bool success = false;
    std::string migration_id;
    std::string error;
    std::string diagnostics;
    DatabaseVerificationResult verification;
    ArchiveResult archive;
};

class MigrationOrchestrator {
public:
    MigrationOrchestrator(
        const std::string& source_dir,
        const std::string& database_path,
        const std::string& archive_root,
        const std::string& source_version,
        const std::string& target_version);

    MigrationResult migrate_to_sqlite();

    static std::string activation_state_path(const std::string& db_dir);

private:
    struct StageReport {
        std::string stage_name;
        bool ok = false;
        std::string detail;
    };

    void append_stage(std::vector<StageReport>& stages,
                      const std::string& name, bool ok,
                      const std::string& detail);

    bool atomic_write_json(const std::string& path,
                           const std::string& content);

    std::string source_dir_;
    std::string database_path_;
    std::string archive_root_;
    std::string source_version_;
    std::string target_version_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_MIGRATION_ORCHESTRATOR_H
