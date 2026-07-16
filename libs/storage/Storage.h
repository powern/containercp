#ifndef CONTAINERCP_STORAGE_STORAGE_H
#define CONTAINERCP_STORAGE_STORAGE_H

#include "ConnectionPool.h"
#include "SQLiteStorage.h"
#include "access/AccessGrant.h"
#include "access/AccessUser.h"
#include "auth/AuthUser.h"
#include "profile/Profile.h"
#include "proxy/ReverseProxy.h"
#include "backup/Backup.h"
#include "database/Database.h"
#include "mail/MailAlias.h"
#include "mail/Mailbox.h"
#include "mail/MailDomain.h"
#include "ssl/SslCertificate.h"
#include "domain/Domain.h"
#include "node/Node.h"
#include "php/PhpVersion.h"
#include "site/Site.h"
#include "user/User.h"

#include <string>
#include <vector>

namespace containercp::storage {

class Storage {
public:
    explicit Storage(const std::string& db_path);

    // Nodes — SQLite-backed
    void save_nodes(const std::vector<node::Node>& nodes);
    std::vector<node::Node> load_nodes();

    // Sites — TXT-backed
    void save_sites(const std::vector<site::Site>& sites);
    std::vector<site::Site> load_sites();

    // Users — TXT-backed
    void save_users(const std::vector<user::User>& users);
    std::vector<user::User> load_users();

    // Domains — TXT-backed
    void save_domains(const std::vector<domain::Domain>& domains);
    std::vector<domain::Domain> load_domains();

    // PHP versions — SQLite-backed
    void save_php_versions(const std::vector<php::PhpVersion>& versions);
    std::vector<php::PhpVersion> load_php_versions();

    // Databases — TXT-backed
    void save_databases(const std::vector<database::Database>& databases);
    std::vector<database::Database> load_databases();

    // Backups — TXT-backed
    void save_backups(const std::vector<backup::Backup>& backups);
    std::vector<backup::Backup> load_backups();

    // SSL certificates — TXT-backed
    void save_ssl_certificates(const std::vector<ssl::SslCertificate>& certs);
    std::vector<ssl::SslCertificate> load_ssl_certificates();

    // Mail — TXT-backed
    void save_mail_domains(const std::vector<mail::MailDomain>& domains);
    std::vector<mail::MailDomain> load_mail_domains();
    void save_mail_module_state(const std::string& state);
    std::string load_mail_module_state();
    void save_mail_smarthost(const std::string& config);
    std::string load_mail_smarthost();
    void save_mailboxes(const std::vector<mail::Mailbox>& mailboxes);
    std::vector<mail::Mailbox> load_mailboxes();
    void save_mail_aliases(const std::vector<mail::MailAlias>& aliases);
    std::vector<mail::MailAlias> load_mail_aliases();

    // Access — TXT-backed
    void save_access_users(const std::vector<access::AccessUser>& users);
    std::vector<access::AccessUser> load_access_users();
    void save_access_grants(const std::vector<access::AccessGrant>& grants);
    std::vector<access::AccessGrant> load_access_grants();

    // Reverse proxies — TXT-backed
    void save_reverse_proxies(const std::vector<proxy::ReverseProxy>& proxies);
    std::vector<proxy::ReverseProxy> load_reverse_proxies();

    // Profiles — SQLite-backed
    void save_profiles(const std::vector<profile::Profile>& profiles);
    std::vector<profile::Profile> load_profiles();
    std::vector<profile::Profile> migrate_template_profiles();

    // Auth users — TXT-backed
    void save_auth_users(const std::vector<auth::AuthUser>& users);
    std::vector<auth::AuthUser> load_auth_users();

    // Transaction support (forward-looking — TXT backend returns false)
    bool begin_transaction();
    bool commit_transaction();
    bool rollback_transaction();
    bool backup(const std::string& dest_path);

private:
    std::string nodes_file() const;
    std::string sites_file() const;
    std::string users_file() const;
    std::string domains_file() const;
    std::string php_versions_file() const;
    std::string databases_file() const;
    std::string backups_file() const;
    std::string ssl_certificates_file() const;
    std::string mail_domains_file() const;
    std::string mailboxes_file() const;
    std::string mail_aliases_file() const;
    std::string mail_state_file() const;
    std::string mail_smarthost_file() const;
    std::string access_users_file() const;
    std::string access_grants_file() const;
    std::string reverse_proxies_file() const;
    std::string profiles_file() const;
    std::string template_profiles_file() const;
    std::string auth_users_file() const;
    std::string sqlite_db_path() const;

    std::string db_path_;
    ConnectionPool pool_;
    SQLiteStorage sqlite_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_STORAGE_H
