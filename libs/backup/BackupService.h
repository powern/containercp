#ifndef CONTAINERCP_BACKUP_BACKUP_SERVICE_H
#define CONTAINERCP_BACKUP_BACKUP_SERVICE_H

#include "backup/BackupManager.h"
#include "backup/BackupProvider.h"
#include "database/DatabaseDumpService.h"
#include "database/DatabaseManager.h"
#include "jobs/Job.h"
#include "runtime/RuntimeActionExecutor.h"
#include "site/SiteManager.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace containercp::backup {

struct BackupManifest {
    std::string schema_version = "1";
    std::string containercp_version = "0.7.0";
    uint64_t backup_id = 0;
    uint64_t site_id = 0;
    std::string site_domain;
    std::string created_at;
    std::string backup_type = "manual";
    std::string files_status = "included";
    std::string database_status = "included";
    std::string database_engine = "mariadb";
    std::string database_name;
    std::string database_ownership_state = "managed";
    std::string sql_dump_path = "backup-root/database/managed.sql";
    uint64_t sql_dump_size = 0;
    std::string sql_dump_checksum;
    std::string archive_checksum;
    std::string restore_capability = "full,files_only,database_only";
    std::string backup_completeness = "complete";
    std::vector<std::string> warnings;
    std::string compatibility = "db5";
};

struct BackupServiceResult {
    bool success = false;
    std::string code;
    std::string message;
    uint64_t backup_id = 0;
    uint64_t site_id = 0;
    uint64_t recovery_backup_id = 0;
    std::vector<jobs::JobStep> steps;
    jobs::JobFailureDiagnostics failure;
};

struct BackupDownload {
    bool success = false;
    std::string code;
    std::string message;
    std::string filename;
    std::string content_type = "application/gzip";
    std::string body;
};

class BackupService {
public:
    BackupService(site::SiteManager& sites,
                  database::DatabaseManager& databases,
                  BackupManager& backups,
                  BackupProvider& provider,
                  database::DatabaseDumpService& database_dump,
                  runtime::RuntimeActionExecutor& runtime_executor,
                  std::filesystem::path data_root,
                  std::filesystem::path sites_root);

    BackupServiceResult create_site_backup(uint64_t site_id, uint64_t job_id, const std::string& backup_type = "manual");
    BackupServiceResult restore_backup(uint64_t backup_id, uint64_t target_site_id, const std::string& mode, const std::string& confirmation, uint64_t job_id);
    BackupServiceResult remove_backup(uint64_t backup_id);

    std::string backups_json() const;
    std::string backup_json(const Backup& backup) const;
    std::optional<std::filesystem::path> backup_path(uint64_t backup_id) const;
    BackupDownload download_backup(uint64_t backup_id) const;
    std::optional<BackupManifest> read_manifest(const Backup& backup) const;
    void cleanup_staging();

private:
    site::Site* site_for_backup_target(uint64_t site_id) const;
    database::Database* managed_database_for_site(uint64_t site_id) const;
    BackupServiceResult create_site_backup_internal(uint64_t site_id, uint64_t job_id, const std::string& backup_type, uint64_t forced_id);
    BackupServiceResult restore_backup_internal(uint64_t backup_id, uint64_t target_site_id, const std::string& mode, const std::string& confirmation, uint64_t job_id, bool create_recovery);
    BackupManifest manifest_for_legacy(const Backup& backup) const;
    Backup backup_record_from_manifest(const BackupManifest& manifest, const std::string& filename, uint64_t size, const std::filesystem::path& path) const;

    site::SiteManager& sites_;
    database::DatabaseManager& databases_;
    BackupManager& backups_;
    BackupProvider& provider_;
    database::DatabaseDumpService& database_dump_;
    runtime::RuntimeActionExecutor& runtime_executor_;
    std::filesystem::path data_root_;
    std::filesystem::path sites_root_;
};

std::string backup_manifest_to_json(const BackupManifest& manifest);
std::optional<BackupManifest> backup_manifest_from_json(const std::string& json);

} // namespace containercp::backup

#endif // CONTAINERCP_BACKUP_BACKUP_SERVICE_H
