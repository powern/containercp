#ifndef CONTAINERCP_STORAGE_STORAGE_H
#define CONTAINERCP_STORAGE_STORAGE_H

#include "access/AccessUser.h"
#include "backup/Backup.h"
#include "database/Database.h"
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

    void save_nodes(const std::vector<node::Node>& nodes);
    std::vector<node::Node> load_nodes();

    void save_sites(const std::vector<site::Site>& sites);
    std::vector<site::Site> load_sites();

    void save_users(const std::vector<user::User>& users);
    std::vector<user::User> load_users();

    void save_domains(const std::vector<domain::Domain>& domains);
    std::vector<domain::Domain> load_domains();

    void save_php_versions(const std::vector<php::PhpVersion>& versions);
    std::vector<php::PhpVersion> load_php_versions();

    void save_databases(const std::vector<database::Database>& databases);
    std::vector<database::Database> load_databases();

    void save_backups(const std::vector<backup::Backup>& backups);
    std::vector<backup::Backup> load_backups();

    void save_ssl_certificates(const std::vector<ssl::SslCertificate>& certs);
    std::vector<ssl::SslCertificate> load_ssl_certificates();

    void save_mail_domains(const std::vector<mail::MailDomain>& domains);
    std::vector<mail::MailDomain> load_mail_domains();

    void save_access_users(const std::vector<access::AccessUser>& users);
    std::vector<access::AccessUser> load_access_users();

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
    std::string access_users_file() const;

    std::string db_path_;
};

} // namespace containercp::storage

#endif // CONTAINERCP_STORAGE_STORAGE_H
