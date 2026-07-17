#ifndef CONTAINERCP_STORAGE_LEGACY_IMPORTER_H
#define CONTAINERCP_STORAGE_LEGACY_IMPORTER_H

#include "ConnectionPool.h"
#include "LegacyDatasetReader.h"
#include "LegacyFileInventory.h"
#include "SQLiteStorage.h"
#include "access/AccessGrant.h"
#include "access/AccessUser.h"
#include "auth/AuthUser.h"
#include "backup/Backup.h"
#include "mail/MailAlias.h"
#include "mail/MailDomain.h"
#include "mail/Mailbox.h"
#include "database/Database.h"
#include "domain/Domain.h"
#include "node/Node.h"
#include "php/PhpVersion.h"
#include "profile/Profile.h"
#include "proxy/ReverseProxy.h"
#include "site/Site.h"
#include "ssl/SslCertificate.h"
#include "user/User.h"

#include <set>
#include <string>
#include <vector>

namespace containercp::storage {

enum class ImportDisposition {
    Imported,
    SkippedMissingOptional,
    SkippedEmpty,
    Failed
};

struct ResourceBaseline {
    bool success = false;
    uint64_t record_count = 0;
    std::string canonical_checksum;
    std::string error;
};

struct ImportResult {
    bool success = false;
    ImportDisposition disposition = ImportDisposition::Failed;
    std::string resource_type;
    std::string source_file;
    uint64_t record_count = 0;
    std::string error;
    std::string diagnostics;

    // Baseline captured just before import (for skipped-resource verification)
    ResourceBaseline baseline;
};

struct ImportAllResult {
    bool success = false;
    std::vector<ImportResult> resources;
    std::string failed_resource;
    std::string error;
};

// Legacy TXT-to-SQLite importer.
//
// Reads TXT files from legacy_directory and writes parsed records into
// the SQLite database.  Source files are never modified.  Invoked
// explicitly — never on startup or Storage construction.
//
// Importer success requires confirmed SQLite commit.
// Phase 9 (migration verification) is implemented in Verification.h/.cpp.
class LegacyImporter {
public:
    LegacyImporter(const std::string& legacy_directory, ConnectionPool& pool);

    ImportResult import_nodes();
    ImportResult import_php_versions();
    ImportResult import_profiles();
    ImportResult import_template_profiles();
    ImportResult import_all_profiles();
    ImportResult import_users();
    ImportResult import_sites();
    ImportResult import_domains();
    ImportResult import_databases();
    ImportResult import_backups();
    ImportResult import_reverse_proxies();
    ImportResult import_access_users();
    ImportResult import_access_grants();
    ImportResult import_auth_users();
    ImportResult import_ssl_certificates();
    ImportResult import_mail_domains();
    ImportResult import_mail_mailboxes();
    ImportResult import_mail_aliases();
    ImportResult import_mail_config();

    ImportAllResult import_all();

private:
    struct ParseResult {
        bool success = false;
        std::string error;
        std::string diagnostics;
    };

    // Shared profile parser — parses one file into a vector, detecting
    // duplicates within the file.  Returns empty result on success.
    static ParseResult parse_profile_file(
        const std::string& path,
        const std::string& fname,
        bool template_format,
        std::vector<profile::Profile>& out,
        std::set<uint64_t>& ids,
        std::set<std::string>& names);

    // File state classification
    enum class FileState {
        Missing,
        RegularReadable,
        Empty,
        Unreadable,
        InvalidType,
        ReadError
    };

    FileState inspect_file(const std::string& filename) const;

    // Helper to centralise file-inspection and result construction.
    // On RegularReadable it returns a success result with empty fields;
    // caller fills in record_count, parses, and calls checked_saver.
    // On failure/non-readable it returns a complete failure/skip result.
    ImportResult inspect_and_begin(
        const std::string& type,
        const std::string& filename,
        bool required);

    // Checked saver wrapper — centralises FK-failure detection.
    // Returns ImportResult with success/error populated.
    ImportResult finish_import(
        ImportResult r,
        bool write_ok,
        uint64_t count);

    // Baseline capture — checked per-resource.
    // Fails with success=false on connection/prepare/step errors.
    ResourceBaseline capture_baseline(const std::string& type);

    std::string legacy_dir_;
    ConnectionPool& pool_;
    SQLiteStorage sqlite_;
    LegacyDatasetReader reader_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_LEGACY_IMPORTER_H
