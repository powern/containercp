#ifndef CONTAINERCP_STORAGE_LEGACY_IMPORTER_H
#define CONTAINERCP_STORAGE_LEGACY_IMPORTER_H

#include "ConnectionPool.h"
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

#include <string>
#include <vector>

namespace containercp::storage {

enum class ImportDisposition {
    Imported,
    SkippedMissingOptional,
    SkippedEmpty,
    Failed
};

struct ImportResult {
    bool success = false;
    ImportDisposition disposition = ImportDisposition::Failed;
    std::string resource_type;
    std::string source_file;
    uint64_t record_count = 0;
    std::string error;       // safe user-facing error (no secrets)
    std::string diagnostics; // detailed internal diagnostics (no secrets)
};

struct ImportAllResult {
    bool success = false;
    std::vector<ImportResult> resources;
    std::string failed_resource;
    std::string error;
};

// Strict legacy TXT-to-SQLite importer for ContainerCP v0.6.0 formats.
//
// Reads TXT files from legacy_directory and writes parsed records into
// the SQLite database managed by the given ConnectionPool.
//
// Source TXT files are never modified — the importer is read-only.
// SQLiteStorage must already have the schema initialized.
//
// Import is invoked explicitly (never during normal startup or Storage
// construction).
class LegacyImporter {
public:
    LegacyImporter(const std::string& legacy_directory, ConnectionPool& pool);

    // Per-resource import methods
    ImportResult import_nodes();
    ImportResult import_php_versions();
    ImportResult import_profiles();
    ImportResult import_template_profiles();
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

    // Import all resources in dependency-safe order.
    // Returns the first failure; prior resources remain committed.
    ImportAllResult import_all();

private:
    // Parser helpers — return parsed records or a descriptive error
    struct ParseResult {
        bool success = false;
        std::string error;
        std::string diagnostics;
        int line_number = 0;
    };

    ParseResult parse_nodes(std::vector<node::Node>& out);
    ParseResult parse_php_versions(std::vector<php::PhpVersion>& out);
    ParseResult parse_profiles(std::vector<profile::Profile>& out);
    ParseResult parse_template_profiles(std::vector<profile::Profile>& out);
    ParseResult parse_users(std::vector<user::User>& out);
    ParseResult parse_sites(std::vector<site::Site>& out);
    ParseResult parse_domains(std::vector<domain::Domain>& out);
    ParseResult parse_databases(std::vector<database::Database>& out);
    ParseResult parse_backups(std::vector<backup::Backup>& out);
    ParseResult parse_reverse_proxies(std::vector<proxy::ReverseProxy>& out);
    ParseResult parse_access_users(std::vector<access::AccessUser>& out);
    ParseResult parse_access_grants(std::vector<access::AccessGrant>& out);
    ParseResult parse_auth_users(std::vector<auth::AuthUser>& out);
    ParseResult parse_ssl_certificates(std::vector<ssl::SslCertificate>& out);
    ParseResult parse_mail_domains(std::vector<mail::MailDomain>& out);
    ParseResult parse_mail_mailboxes(std::vector<mail::Mailbox>& out);
    ParseResult parse_mail_aliases(std::vector<mail::MailAlias>& out);

    // Config file readers (singletons)
    ParseResult read_mail_module_state(std::string& state);
    ParseResult read_smarthost_config(std::string& config);

    // File existence helpers
    enum class FilePresence { Required, Optional };
    bool file_exists(const std::string& filename) const;
    std::string file_path(const std::string& filename) const;

    // Save parsed records via SQLiteStorage
    ImportResult do_import(
        const std::string& type,
        const std::string& filename,
        FilePresence presence,
        const std::function<ParseResult()>& parser,
        const std::function<void()>& saver);

    std::string legacy_dir_;
    ConnectionPool& pool_;
    SQLiteStorage sqlite_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_LEGACY_IMPORTER_H
