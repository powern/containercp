#ifndef CONTAINERCP_MIGRATION_VESTA_SITE_IMPORTER_H
#define CONTAINERCP_MIGRATION_VESTA_SITE_IMPORTER_H

#include "config/Config.h"
#include "core/OperationResult.h"
#include "database/DatabaseManager.h"
#include "domain/DomainManager.h"
#include "filesystem/Filesystem.h"
#include "logger/Logger.h"
#include "proxy/ProxyProvider.h"
#include "proxy/ReverseProxyManager.h"
#include "provider/HostingProvider.h"
#include "runtime/CommandExecutor.h"
#include "site/SiteManager.h"

#include <cstdint>
#include <string>
#include <vector>

namespace containercp::migration {

struct Manifest {
    std::string domain;
    std::string backup_path;
    size_t archive_size = 0;
    bool domain_found = false;
    std::string web_archive_path;
    bool web_size_known = false;
    size_t web_size = 0;
    std::string web_root_type;
    bool wp_config_found = false;
    bool wp_config_parsed = false;
    std::string wp_db_name;
    std::string wp_db_user;
    std::string wp_db_host;
    bool wp_db_ambiguous = false;
    bool db_dump_found = false;
    std::string db_dump_path;
    bool db_dump_size_known = false;
    size_t db_dump_size = 0;
    std::string db_type;
    std::vector<std::string> all_databases;
    bool site_exists = false;
    bool migration_marker_found = false;
    uint64_t migration_stage = 0;
    bool files_pending = false;
    bool files_imported = false;
    bool sql_pending = false;
    bool migration_ready_for_files = false;
    uint64_t migration_site_id = 0;
    std::string migration_owner;
    std::string files_status;
    std::string sql_status;
    std::string marker_error;
    uint64_t available_disk_mb = 0;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

struct Options {
    std::string backup_path;
    std::string domain;
    std::string owner;
    std::string database;
    bool dry_run = false;
    bool keep_staging = false;
    bool skip_db = false;
};

class VestaSiteImporter {
public:
    VestaSiteImporter(runtime::CommandExecutor& executor,
                      filesystem::Filesystem& fs, config::Config& cfg,
                      logger::Logger& logger,
                      site::SiteManager* sites = nullptr,
                      domain::DomainManager* domains = nullptr);

    Manifest inspect(const Options& opts);
    std::string format_dry_run(const Manifest& m, const Options& opts);

    struct ImportFilesResult {
        bool success = false;
        std::string web_root_type;
        size_t files_count = 0;
        size_t bytes_copied = 0;
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
    };

    ImportFilesResult import_files(const Options& opts);

private:
    bool tar_safe_list(const std::string& archive,
                       std::vector<std::string>& entries,
                       std::string& error);
    bool find_domain_in_archive(const std::vector<std::string>& entries,
                                const std::string& domain,
                                std::string& web_archive_path,
                                size_t& web_size,
                                bool& size_known);
    std::string detect_web_root(const std::string& staging_dir,
                                const std::string& archive,
                                const std::string& domain);
    bool extract_wp_config(const std::string& archive,
                           const std::string& domain,
                           const std::string& web_root_type,
                           std::string& staging_dir,
                           std::string& out_db_name,
                           std::string& out_db_user,
                           std::string& out_db_password,
                           std::string& out_db_host,
                           bool& out_parsed,
                           bool& out_ambiguous);
    bool find_db_in_archive(const std::vector<std::string>& entries,
                            const std::string& db_name,
                            std::string& out_dump_path,
                            size_t& out_size,
                            bool& size_known,
                            std::string& out_type);
    std::string normalize_db_name(const std::string& raw) const;
    std::string make_staging_dir();
    void cleanup_staging(const std::string& dir);
    bool extract_web_archive(const std::string& archive, const std::string& domain,
                              const std::string& staging_dir,
                              std::string& out_data_tarball);
    bool copy_files_to_public(const std::string& staging_dir,
                               const std::string& web_root_type,
                               const std::string& site_dir,
                               ImportFilesResult& result,
                               const std::string& uid_str = "1000",
                               const std::string& gid_str = "1000");

    runtime::CommandExecutor& executor_;
    filesystem::Filesystem& fs_;
    config::Config& cfg_;
    logger::Logger& logger_;
    site::SiteManager* sites_ = nullptr;
    domain::DomainManager* domains_ = nullptr;
};

} // namespace containercp::migration

#endif
