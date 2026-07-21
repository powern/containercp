#ifndef CONTAINERCP_DATABASE_DATABASE_DUMP_SERVICE_H
#define CONTAINERCP_DATABASE_DATABASE_DUMP_SERVICE_H

#include "database/DatabaseManager.h"
#include "database/DatabaseProvider.h"
#include "jobs/Job.h"
#include "runtime/SiteRuntimeManager.h"
#include "site/SiteManager.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace containercp::database {

struct DatabaseArtifactMetadata {
    std::string artifact_id;
    uint64_t database_id = 0;
    uint64_t site_id = 0;
    uint64_t job_id = 0;
    std::string kind;
    std::string sanitized_filename;
    uint64_t size = 0;
    std::string checksum_sha256;
    std::string created_at;
    std::string expires_at;
    int64_t expires_at_epoch = 0;
    std::string status;
    uint64_t download_count = 0;
    bool cleanup_state = false;
};

struct DatabaseDumpResult {
    bool success = false;
    std::string code;
    std::string message;
    std::vector<jobs::JobStep> steps;
    jobs::JobFailureDiagnostics failure;
    bool manual_recovery_required = false;
    std::string artifact_id;
    std::string recovery_artifact_id;
};

struct DatabaseUploadResult {
    bool success = false;
    std::string code;
    std::string message;
    std::string artifact_id;
    uint64_t database_id = 0;
    uint64_t site_id = 0;
};

class DatabaseDumpService {
public:
    using RuntimeDbStatusLookup = std::function<std::string(const site::Site&)>;

    static constexpr uint64_t kMaxImportSizeBytes = 5 * 1024 * 1024;
    static constexpr int64_t kDefaultExpirySeconds = 24 * 60 * 60;

    DatabaseDumpService(site::SiteManager& sites,
                        DatabaseManager& databases,
                        runtime::SiteRuntimeManager& site_runtime,
                        const DatabaseProvider& provider,
                        std::filesystem::path sites_root,
                        std::filesystem::path artifacts_root);

    DatabaseDumpService(site::SiteManager& sites,
                        DatabaseManager& databases,
                        RuntimeDbStatusLookup runtime_lookup,
                        const DatabaseProvider& provider,
                        std::filesystem::path sites_root,
                        std::filesystem::path artifacts_root);

    DatabaseDumpResult exportManagedDatabase(uint64_t database_id, uint64_t job_id, const std::string& artifact_id);
    DatabaseDumpResult importManagedDatabase(uint64_t database_id, uint64_t job_id, const std::string& artifact_id, const std::string& confirmation);
    DatabaseUploadResult stageImportUpload(uint64_t database_id, const std::string& original_filename, const std::string& content);

    std::optional<DatabaseArtifactMetadata> artifact(uint64_t database_id, const std::string& artifact_id) const;
    std::optional<std::filesystem::path> artifact_path(uint64_t database_id, const std::string& artifact_id) const;
    DatabaseUploadResult revokeArtifact(uint64_t database_id, const std::string& artifact_id);
    void record_download(uint64_t database_id, const std::string& artifact_id);
    void cleanup_expired();

    bool can_transfer(const Database& database) const;
    std::string transfer_block_reason(const Database& database) const;

private:
    struct ResolvedTarget {
        site::Site* site_record = nullptr;
        Database* database = nullptr;
        MariaDBConnectionTarget target;
        DatabaseProviderCredential service_account;
    };

    DatabaseDumpResult resolve_target(uint64_t database_id, ResolvedTarget& resolved, const std::string& operation, uint64_t job_id) const;
    DatabaseProviderCredential load_service_account(const site::Site& site_record) const;
    MariaDBConnectionTarget target_for_site(const site::Site& site_record) const;
    bool site_has_exactly_one_database(uint64_t site_id) const;
    std::filesystem::path artifact_dir(const std::string& artifact_id) const;
    std::filesystem::path sql_path(const std::string& artifact_id) const;
    std::filesystem::path metadata_path(const std::string& artifact_id) const;
    bool write_metadata(const DatabaseArtifactMetadata& metadata) const;
    std::optional<DatabaseArtifactMetadata> read_metadata(const std::string& artifact_id) const;
    DatabaseDumpResult finalize_export_artifact(const ResolvedTarget& target,
                                                uint64_t job_id,
                                                const std::string& artifact_id,
                                                const std::filesystem::path& tmp_path,
                                                std::vector<jobs::JobStep> steps,
                                                const std::string& kind);
    DatabaseDumpResult create_recovery_export(const ResolvedTarget& target, uint64_t job_id);

    site::SiteManager& sites_;
    DatabaseManager& databases_;
    runtime::SiteRuntimeManager* site_runtime_ = nullptr;
    RuntimeDbStatusLookup runtime_lookup_;
    const DatabaseProvider& provider_;
    std::filesystem::path sites_root_;
    std::filesystem::path artifacts_root_;
};

bool database_artifact_id_valid(const std::string& artifact_id);
std::string database_generate_artifact_id();
std::string database_sanitize_dump_filename(const std::string& filename, const std::string& fallback_prefix);
bool database_import_content_policy_allows(const std::filesystem::path& path, std::string& code, std::string& message);
std::string database_artifact_metadata_json(const DatabaseArtifactMetadata& metadata);

} // namespace containercp::database

#endif // CONTAINERCP_DATABASE_DATABASE_DUMP_SERVICE_H
