#ifndef CONTAINERCP_STORAGE_LEGACY_DATASET_READER_H
#define CONTAINERCP_STORAGE_LEGACY_DATASET_READER_H

#include "access/AccessGrant.h"
#include "access/AccessUser.h"
#include "auth/AuthUser.h"
#include "backup/Backup.h"
#include "database/Database.h"
#include "domain/Domain.h"
#include "mail/MailAlias.h"
#include "mail/Mailbox.h"
#include "mail/MailDomain.h"
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

template<typename T>
struct DatasetResult {
    bool success = false;
    std::vector<T> records;
    std::string error;
    std::string diagnostics;
};

// Read-only legacy TXT dataset reader.
//
// Provides normalized C++ model records parsed from ContainerCP v0.6.0
// pipe-delimited TXT files.  Used by both LegacyImporter (for import)
// and Verification (for comparison).  Single source of truth for all
// legacy parsing.
//
// Source files are never modified.  No SQLite writes occur.
class LegacyDatasetReader {
public:
    explicit LegacyDatasetReader(const std::string& legacy_directory);

    DatasetResult<node::Node> read_nodes();
    DatasetResult<php::PhpVersion> read_php_versions();
    DatasetResult<profile::Profile> read_combined_profiles();
    DatasetResult<profile::Profile> read_profiles_only();
    DatasetResult<profile::Profile> read_templates_only();
    DatasetResult<user::User> read_users();
    DatasetResult<site::Site> read_sites();
    DatasetResult<domain::Domain> read_domains();
    DatasetResult<database::Database> read_databases();
    DatasetResult<backup::Backup> read_backups();
    DatasetResult<proxy::ReverseProxy> read_reverse_proxies();
    DatasetResult<access::AccessUser> read_access_users();
    DatasetResult<access::AccessGrant> read_access_grants();
    DatasetResult<auth::AuthUser> read_auth_users();
    DatasetResult<ssl::SslCertificate> read_ssl_certificates();
    DatasetResult<mail::MailDomain> read_mail_domains();
    DatasetResult<mail::Mailbox> read_mailboxes();
    DatasetResult<mail::MailAlias> read_mail_aliases();

    struct MailConfigResult {
        bool success = false;
        bool module_state_present = false;
        std::string module_state;
        bool smarthost_present = false;
        std::string smarthost;
        std::string error;
    };
    MailConfigResult read_mail_config();

    // File presence check
    struct FileInfo {
        bool exists = false;
        bool empty = false;
    };
    FileInfo check_file(const std::string& filename) const;

private:
    std::string legacy_dir_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_LEGACY_DATASET_READER_H
