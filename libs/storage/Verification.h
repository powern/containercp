#ifndef CONTAINERCP_STORAGE_VERIFICATION_H
#define CONTAINERCP_STORAGE_VERIFICATION_H

#include "ConnectionPool.h"
#include "LegacyImporter.h"

#include <map>
#include <string>
#include <vector>

namespace containercp::storage {

enum class VerificationStatus {
    Passed,
    Failed,
    Skipped
};

struct FieldMismatch {
    std::string resource_type;
    uint64_t record_id = 0;
    std::string source;
    std::string field;
    std::string expected;
    std::string actual;
};

struct ResourceVerificationResult {
    bool success = false;
    VerificationStatus status = VerificationStatus::Failed;

    std::string resource_type;

    uint64_t legacy_record_count = 0;
    uint64_t sqlite_record_count = 0;

    std::string legacy_checksum;
    std::string sqlite_checksum;

    std::vector<FieldMismatch> mismatches;

    std::string error;
    std::string diagnostics;
};

struct DatabaseVerificationResult {
    bool success = false;

    std::vector<ResourceVerificationResult> resources;
    std::vector<ResourceVerificationResult> reopened_resources;

    std::string initial_integrity_check_result;
    std::string reopened_integrity_check_result;
    std::vector<std::string> initial_foreign_key_violations;
    std::vector<std::string> reopened_foreign_key_violations;

    bool initial_verification_passed = false;
    bool reopen_succeeded = false;
    bool reopened_verification_passed = false;

    std::string error;
};

// Migration verification for Phase 8 imported data.
//
// Compares canonical legacy data (parsed from TXT) against SQLite data
// loaded through explicit queries.  Verification is invoked explicitly
// — never on daemon startup.
//
// Phase 10 archive and Phase 11 startup gate are not implemented.
class Verification {
public:
    Verification(const std::string& legacy_directory,
                 const std::string& sqlite_path,
                 const ImportAllResult& import_result,
                 const std::string& storage_directory = "");

    ResourceVerificationResult verify_nodes();
    ResourceVerificationResult verify_php_versions();
    ResourceVerificationResult verify_profiles();
    ResourceVerificationResult verify_users();
    ResourceVerificationResult verify_sites();
    ResourceVerificationResult verify_domains();
    ResourceVerificationResult verify_databases();
    ResourceVerificationResult verify_backups();
    ResourceVerificationResult verify_reverse_proxies();
    ResourceVerificationResult verify_access_users();
    ResourceVerificationResult verify_access_grants();
    ResourceVerificationResult verify_auth_users();
    ResourceVerificationResult verify_ssl_certificates();
    ResourceVerificationResult verify_mail_domains();
    ResourceVerificationResult verify_mail_mailboxes();
    ResourceVerificationResult verify_mail_aliases();
    ResourceVerificationResult verify_mail_config();

    DatabaseVerificationResult verify_all();

    static std::string sha256(const std::string& data);

    // Canonical serialization helpers (public so free comparison functions and tests can use them)
    static void append_field(std::string& out, const std::string& value);
    static void append_field(std::string& out, uint64_t value);

    // Per-resource canonical serialization (public for test verification)
    std::string canonical_nodes(const std::vector<node::Node>& records);
    std::string canonical_php_versions(const std::vector<php::PhpVersion>& records);
    std::string canonical_profiles(const std::vector<profile::Profile>& records);
    std::string canonical_users(const std::vector<user::User>& records);
    std::string canonical_sites(const std::vector<site::Site>& records);
    std::string canonical_domains(const std::vector<domain::Domain>& records);
    std::string canonical_databases(const std::vector<database::Database>& records);
    std::string canonical_backups(const std::vector<backup::Backup>& records);
    std::string canonical_reverse_proxies(const std::vector<proxy::ReverseProxy>& records);
    std::string canonical_access_users(const std::vector<access::AccessUser>& records);
    std::string canonical_access_grants(const std::vector<access::AccessGrant>& records);
    std::string canonical_auth_users(const std::vector<auth::AuthUser>& records);
    std::string canonical_ssl_certificates(const std::vector<ssl::SslCertificate>& records);
    std::string canonical_mail_domains(const std::vector<mail::MailDomain>& records);
    std::string canonical_mail_mailboxes(const std::vector<mail::Mailbox>& records);
    std::string canonical_mail_aliases(const std::vector<mail::MailAlias>& records);
    std::string canonical_mail_config(bool ms_present, const std::string& ms,
                                       bool sh_present, const std::string& sh);

private:

    // Checked SQLite loads
    ResourceVerificationResult load_sqlite_nodes(std::vector<node::Node>& out);
    ResourceVerificationResult load_sqlite_php_versions(std::vector<php::PhpVersion>& out);
    ResourceVerificationResult load_sqlite_profiles(std::vector<profile::Profile>& out);
    ResourceVerificationResult load_sqlite_users(std::vector<user::User>& out);
    ResourceVerificationResult load_sqlite_sites(std::vector<site::Site>& out);
    ResourceVerificationResult load_sqlite_domains(std::vector<domain::Domain>& out);
    ResourceVerificationResult load_sqlite_databases(std::vector<database::Database>& out);
    ResourceVerificationResult load_sqlite_backups(std::vector<backup::Backup>& out);
    ResourceVerificationResult load_sqlite_reverse_proxies(std::vector<proxy::ReverseProxy>& out);
    ResourceVerificationResult load_sqlite_access_users(std::vector<access::AccessUser>& out);
    ResourceVerificationResult load_sqlite_access_grants(std::vector<access::AccessGrant>& out);
    ResourceVerificationResult load_sqlite_auth_users(std::vector<auth::AuthUser>& out);
    ResourceVerificationResult load_sqlite_ssl_certificates(std::vector<ssl::SslCertificate>& out);
    ResourceVerificationResult load_sqlite_mail_domains(std::vector<mail::MailDomain>& out);
    ResourceVerificationResult load_sqlite_mail_mailboxes(std::vector<mail::Mailbox>& out);
    ResourceVerificationResult load_sqlite_mail_aliases(std::vector<mail::MailAlias>& out);

    // Find the import disposition for a resource type
    const ImportResult* find_import_result(const std::string& type) const;

    // Immutable initial evidence captured after successful verification
    struct InitialEvidence {
        uint64_t legacy_record_count = 0;
        std::string legacy_checksum;
    };
    std::map<std::string, InitialEvidence> initial_evidence_;

    // Typed expected datasets frozen after successful initial verification
    struct TypedInitialEvidence {
        std::vector<node::Node> nodes;
        std::vector<php::PhpVersion> php_versions;
        std::vector<profile::Profile> profiles;
        std::vector<user::User> users;
        std::vector<site::Site> sites;
        std::vector<domain::Domain> domains;
        std::vector<database::Database> databases;
        std::vector<backup::Backup> backups;
        std::vector<proxy::ReverseProxy> reverse_proxies;
        std::vector<access::AccessUser> access_users;
        std::vector<access::AccessGrant> access_grants;
        std::vector<auth::AuthUser> auth_users;
        std::vector<ssl::SslCertificate> ssl_certificates;
        std::vector<mail::MailDomain> mail_domains;
        std::vector<mail::Mailbox> mail_mailboxes;
        std::vector<mail::MailAlias> mail_aliases;
        std::string mail_module_state;
        std::string mail_smarthost;
    } typed_evidence_;

    std::string legacy_dir_;
    std::string sqlite_path_;
    std::string storage_dir_;
    ImportAllResult import_result_;
    ConnectionPool pool_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_VERIFICATION_H
