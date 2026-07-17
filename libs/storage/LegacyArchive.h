#ifndef CONTAINERCP_STORAGE_LEGACY_ARCHIVE_H
#define CONTAINERCP_STORAGE_LEGACY_ARCHIVE_H

#include "Verification.h"
#include <string>
#include <vector>

namespace containercp::storage {

struct LegacyFileInfo {
    std::string filename;
    bool required;
};

// Authoritative physical inventory — used by importer, archive, tests
inline const std::vector<LegacyFileInfo>& legacy_file_inventory() {
    static const std::vector<LegacyFileInfo> inv = {
        {"nodes.db", true}, {"php_versions.db", true}, {"profiles.db", true},
        {"template_profiles.db", false}, {"users.db", true}, {"sites.db", true},
        {"domains.db", true}, {"databases.db", true}, {"backups.db", true},
        {"reverse_proxies.db", true}, {"access_users.db", false},
        {"access_grants.db", false}, {"auth_users.db", false},
        {"ssl_certificates.db", false}, {"mail_domains.db", false},
        {"mail_mailboxes.db", false}, {"mail_aliases.db", false},
        {"mail_state.db", false}, {"mail_smarthost.db", false}
    };
    return inv;
}

struct ArchiveFileEntry {
    std::string filename;
    uint64_t size = 0;
    std::string sha256;
    uint64_t record_count = 0;
    bool optional = false;
    bool present = false;
};

struct ArchiveManifest {
    std::string manifest_version = "1.0";
    std::string migration_id;
    std::string source_version;
    std::string target_version;
    std::string migration_timestamp;
    std::string source_directory;
    std::string archive_directory;
    std::vector<ArchiveFileEntry> files;
    bool checksum_match = false;
    std::string initial_integrity_check;
    std::string reopened_integrity_check;
    int initial_fk_violations = -1;
    int reopened_fk_violations = -1;
    std::string verification_result;
};

struct ArchiveResult {
    bool success = false;
    std::string archive_path;
    ArchiveManifest manifest;
    std::string error;
    std::string diagnostics;
};

class LegacyArchive {
public:
    LegacyArchive(const std::string& source_directory,
                  const std::string& archive_root);

    ArchiveResult create_archive(
        const std::string& migration_id,
        const std::string& source_version,
        const std::string& target_version,
        const DatabaseVerificationResult& verification_result);

    bool verify_archive(const std::string& archive_path,
                        ArchiveManifest* verified_manifest = nullptr);

    bool set_permissions(const std::string& archive_path);

    static std::string sha256_file(const std::string& path);
    static std::string generate_uuid();
    static std::string timestamp_utc();
    static bool safe_version(const std::string& v);
    static bool valid_migration_id(const std::string& id);
    static std::string json_escape(const std::string& s);
    static uint64_t count_records(const std::string& source_dir,
                                   const std::string& filename);

private:
    std::string source_dir_;
    std::string archive_root_;
    std::string migration_timestamp_; // captured once
    bool temp_owned_ = false;
    std::string temp_path_;
};

} // namespace containercp::storage
#endif
