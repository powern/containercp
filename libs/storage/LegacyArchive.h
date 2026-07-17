#ifndef CONTAINERCP_STORAGE_LEGACY_ARCHIVE_H
#define CONTAINERCP_STORAGE_LEGACY_ARCHIVE_H

#include "Verification.h"

#include <string>
#include <vector>

namespace containercp::storage {

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
    std::string integrity_check;
    std::string foreign_key_violations;

    std::string verification_result;
};

struct ArchiveResult {
    bool success = false;
    std::string archive_path;
    ArchiveManifest manifest;
    std::string error;
    std::string diagnostics;
};

// Immutable legacy TXT archive.
//
// Creates a verified archive of the legacy TXT storage files after
// successful migration verification.  Source files are never modified
// or deleted.
//
// Phase 11 (startup migration gate) is not implemented.
// Manual invocation only.
class LegacyArchive {
public:
    LegacyArchive(const std::string& source_directory,
                  const std::string& archive_root);

    ArchiveResult create_archive(
        const std::string& migration_id,
        const std::string& source_version,
        const std::string& target_version,
        const DatabaseVerificationResult& verification_result);

    bool verify_archive(
        const std::string& archive_path,
        ArchiveManifest* verified_manifest = nullptr);

    bool set_permissions(const std::string& archive_path);

    static std::string sha256_file(const std::string& path);
    static std::string generate_uuid();
    static std::string timestamp_utc();

private:
    std::string source_dir_;
    std::string archive_root_;
};

} // namespace containercp::storage

#endif
