#include "Storage.h"

#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

namespace containercp::storage {

Storage::Storage(const std::string& db_path)
    : db_path_(db_path)
{
    ::mkdir(db_path_.c_str(), 0755);
}

std::string Storage::nodes_file() const {
    return db_path_ + "nodes.db";
}

std::string Storage::sites_file() const {
    return db_path_ + "sites.db";
}

std::string Storage::users_file() const {
    return db_path_ + "users.db";
}

std::string Storage::domains_file() const {
    return db_path_ + "domains.db";
}

std::string Storage::php_versions_file() const {
    return db_path_ + "php_versions.db";
}

std::string Storage::databases_file() const {
    return db_path_ + "databases.db";
}

std::string Storage::backups_file() const {
    return db_path_ + "backups.db";
}

std::string Storage::ssl_certificates_file() const {
    return db_path_ + "ssl_certificates.db";
}

std::string Storage::mail_domains_file() const {
    return db_path_ + "mail_domains.db";
}

std::string Storage::access_users_file() const {
    return db_path_ + "access_users.db";
}

std::string Storage::auth_users_file() const {
    return db_path_ + "auth_users.db";
}

std::string Storage::access_grants_file() const {
    return db_path_ + "access_grants.db";
}

std::string Storage::reverse_proxies_file() const {
    return db_path_ + "reverse_proxies.db";
}

void Storage::save_nodes(const std::vector<node::Node>& nodes) {
    std::ofstream file(nodes_file());
    for (const auto& n : nodes) {
        file << n.id << "|" << n.name << "|" << n.type << "\n";
    }
}

std::vector<node::Node> Storage::load_nodes() {
    std::vector<node::Node> nodes;
    std::ifstream file(nodes_file());
    if (!file.is_open()) {
        return nodes;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        node::Node n;
        if (std::getline(ss, token, '|')) n.id = std::stoull(token);
        if (std::getline(ss, token, '|')) n.name = token;
        if (std::getline(ss, token, '|')) n.type = token;
        nodes.push_back(std::move(n));
    }
    return nodes;
}

void Storage::save_sites(const std::vector<site::Site>& sites) {
    std::ofstream file(sites_file());
    for (const auto& s : sites) {
        file << s.id << "|" << s.domain << "|" << s.owner << "|"
             << s.node_id << "|" << s.web_server << "\n";
    }
}

std::vector<site::Site> Storage::load_sites() {
    std::vector<site::Site> sites;
    std::ifstream file(sites_file());
    if (!file.is_open()) {
        return sites;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        site::Site s;
        if (std::getline(ss, token, '|')) s.id = std::stoull(token);
        if (std::getline(ss, token, '|')) s.domain = token;
        if (std::getline(ss, token, '|')) s.owner = token;
        if (std::getline(ss, token, '|')) s.node_id = std::stoull(token);
        if (std::getline(ss, token, '|')) s.web_server = token.empty() ? "apache" : token;
        s.name = s.domain;
        sites.push_back(std::move(s));
    }
    return sites;
}

void Storage::save_users(const std::vector<user::User>& users) {
    std::ofstream file(users_file());
    for (const auto& u : users) {
        file << u.id << "|" << u.username << "|" << u.uid << "|"
             << u.home_directory << "|" << u.shell << "|" << (u.enabled ? "1" : "0") << "\n";
    }
}

std::vector<user::User> Storage::load_users() {
    std::vector<user::User> users;
    std::ifstream file(users_file());
    if (!file.is_open()) {
        return users;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        user::User u;
        if (std::getline(ss, token, '|')) u.id = std::stoull(token);
        if (std::getline(ss, token, '|')) u.username = token;
        if (std::getline(ss, token, '|')) u.uid = std::stoull(token);
        if (std::getline(ss, token, '|')) u.home_directory = token;
        if (std::getline(ss, token, '|')) u.shell = token;
        if (std::getline(ss, token, '|')) u.enabled = (token == "1");
        u.name = u.username;
        users.push_back(std::move(u));
    }
    return users;
}

void Storage::save_domains(const std::vector<domain::Domain>& domains) {
    std::ofstream file(domains_file());
    for (const auto& d : domains) {
        file << d.id << "|" << d.fqdn << "|" << d.owner_id << "|"
             << d.site_id << "|" << d.php_version << "|"
             << (d.ssl_enabled ? "1" : "0") << "|" << (d.enabled ? "1" : "0") << "\n";
    }
}

std::vector<domain::Domain> Storage::load_domains() {
    std::vector<domain::Domain> domains;
    std::ifstream file(domains_file());
    if (!file.is_open()) {
        return domains;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        domain::Domain d;
        if (std::getline(ss, token, '|')) d.id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.fqdn = token;
        if (std::getline(ss, token, '|')) d.owner_id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.site_id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.php_version = token;
        if (std::getline(ss, token, '|')) d.ssl_enabled = (token == "1");
        if (std::getline(ss, token, '|')) d.enabled = (token == "1");
        d.name = d.fqdn;
        domains.push_back(std::move(d));
    }
    return domains;
}

void Storage::save_php_versions(const std::vector<php::PhpVersion>& versions) {
    std::ofstream file(php_versions_file());
    for (const auto& pv : versions) {
        file << pv.id << "|" << pv.version << "|" << pv.image << "|"
             << (pv.enabled ? "1" : "0") << "|" << (pv.default_version ? "1" : "0") << "\n";
    }
}

std::vector<php::PhpVersion> Storage::load_php_versions() {
    std::vector<php::PhpVersion> versions;
    std::ifstream file(php_versions_file());
    if (!file.is_open()) {
        return versions;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        php::PhpVersion pv;
        if (std::getline(ss, token, '|')) pv.id = std::stoull(token);
        if (std::getline(ss, token, '|')) pv.version = token;
        if (std::getline(ss, token, '|')) pv.image = token;
        if (std::getline(ss, token, '|')) pv.enabled = (token == "1");
        if (std::getline(ss, token, '|')) pv.default_version = (token == "1");
        pv.name = pv.version;
        versions.push_back(std::move(pv));
    }
    return versions;
}

void Storage::save_databases(const std::vector<database::Database>& databases) {
    std::ofstream file(databases_file());
    for (const auto& d : databases) {
        file << d.id << "|" << d.db_name << "|" << d.db_user << "|" << d.db_password << "|"
             << d.engine << "|" << d.version << "|" << d.owner_id << "|" << d.site_id << "|"
             << (d.enabled ? "1" : "0") << "\n";
    }
}

std::vector<database::Database> Storage::load_databases() {
    std::vector<database::Database> databases;
    std::ifstream file(databases_file());
    if (!file.is_open()) {
        return databases;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        database::Database d;
        if (std::getline(ss, token, '|')) d.id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.db_name = token;
        if (std::getline(ss, token, '|')) d.db_user = token;
        if (std::getline(ss, token, '|')) d.db_password = token;
        if (std::getline(ss, token, '|')) d.engine = token;
        if (std::getline(ss, token, '|')) d.version = token;
        if (std::getline(ss, token, '|')) d.owner_id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.site_id = std::stoull(token);
        if (std::getline(ss, token, '|')) d.enabled = (token == "1");
        d.name = d.db_name;
        databases.push_back(std::move(d));
    }
    return databases;
}

void Storage::save_backups(const std::vector<backup::Backup>& backups) {
    std::ofstream file(backups_file());
    for (const auto& b : backups) {
        file << b.id << "|" << b.site_id << "|" << b.owner_id << "|"
             << b.filename << "|" << b.type << "|" << b.size << "|"
             << b.created_at << "|" << b.status << "|"
             << b.file_path << "|" << b.compression << "\n";
    }
}

std::vector<backup::Backup> Storage::load_backups() {
    std::vector<backup::Backup> backups;
    std::ifstream file(backups_file());
    if (!file.is_open()) {
        return backups;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        backup::Backup b;
        if (std::getline(ss, token, '|')) b.id = std::stoull(token);
        if (std::getline(ss, token, '|')) b.site_id = std::stoull(token);
        if (std::getline(ss, token, '|')) b.owner_id = std::stoull(token);
        if (std::getline(ss, token, '|')) b.filename = token;
        if (std::getline(ss, token, '|')) b.type = token;
        if (std::getline(ss, token, '|')) b.size = std::stoull(token);
        if (std::getline(ss, token, '|')) b.created_at = token;
        if (std::getline(ss, token, '|')) b.status = token;
        if (std::getline(ss, token, '|')) b.file_path = token;
        if (std::getline(ss, token, '|')) b.compression = token;
        b.name = b.filename;
        backups.push_back(std::move(b));
    }
    return backups;
}

void Storage::save_ssl_certificates(const std::vector<ssl::SslCertificate>& certs) {
    std::ofstream file(ssl_certificates_file());
    for (const auto& c : certs) {
        file << c.id << "|" << c.domain_id << "|" << c.domain << "|"
             << c.provider << "|" << c.certificate_path << "|" << c.key_path << "|"
             << c.expires_at << "|" << c.status << "|" << (c.enabled ? "1" : "0") << "|"
             << (c.auto_renew ? "1" : "0") << "\n";
    }
}

std::vector<ssl::SslCertificate> Storage::load_ssl_certificates() {
    std::vector<ssl::SslCertificate> certs;
    std::ifstream file(ssl_certificates_file());
    if (!file.is_open()) {
        return certs;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        ssl::SslCertificate c;
        if (std::getline(ss, token, '|')) c.id = std::stoull(token);
        if (std::getline(ss, token, '|')) c.domain_id = std::stoull(token);
        if (std::getline(ss, token, '|')) c.domain = token;
        if (std::getline(ss, token, '|')) c.provider = token;
        if (std::getline(ss, token, '|')) c.certificate_path = token;
        if (std::getline(ss, token, '|')) c.key_path = token;
        if (std::getline(ss, token, '|')) c.expires_at = token;
        if (std::getline(ss, token, '|')) c.status = token;
        if (std::getline(ss, token, '|')) c.enabled = (token == "1");
        if (std::getline(ss, token, '|')) c.auto_renew = (token == "1");
        c.name = c.domain;
        certs.push_back(std::move(c));
    }
    return certs;
}

void Storage::save_mail_domains(const std::vector<mail::MailDomain>& domains) {
    std::ofstream file(mail_domains_file());
    for (const auto& m : domains) {
        file << m.id << "|" << m.domain_id << "|" << m.domain << "|"
             << m.owner_id << "|" << (m.enabled ? "1" : "0") << "|" << m.status << "\n";
    }
}

std::vector<mail::MailDomain> Storage::load_mail_domains() {
    std::vector<mail::MailDomain> domains;
    std::ifstream file(mail_domains_file());
    if (!file.is_open()) {
        return domains;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        mail::MailDomain m;
        if (std::getline(ss, token, '|')) m.id = std::stoull(token);
        if (std::getline(ss, token, '|')) m.domain_id = std::stoull(token);
        if (std::getline(ss, token, '|')) m.domain = token;
        if (std::getline(ss, token, '|')) m.owner_id = std::stoull(token);
        if (std::getline(ss, token, '|')) m.enabled = (token == "1");
        if (std::getline(ss, token, '|')) m.status = token;
        m.name = m.domain;
        domains.push_back(std::move(m));
    }
    return domains;
}

void Storage::save_access_users(const std::vector<access::AccessUser>& users) {
    std::ofstream file(access_users_file());
    for (const auto& u : users) {
        file << u.id << "|" << u.username << "|" << u.auth_type << "|"
             << u.password_hash << "|" << (u.enabled ? "1" : "0") << "\n";
    }
}

std::vector<access::AccessUser> Storage::load_access_users() {
    std::vector<access::AccessUser> users;
    std::ifstream file(access_users_file());
    if (!file.is_open()) {
        return users;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        access::AccessUser u;
        if (std::getline(ss, token, '|')) u.id = std::stoull(token);
        if (std::getline(ss, token, '|')) u.username = token;
        if (std::getline(ss, token, '|')) u.auth_type = token;
        if (std::getline(ss, token, '|')) u.password_hash = token;
        if (std::getline(ss, token, '|')) u.enabled = (token == "1");
        u.name = u.username;
        users.push_back(std::move(u));
    }
    return users;
}

void Storage::save_auth_users(const std::vector<auth::AuthUser>& users) {
    std::ofstream file(auth_users_file());
    for (const auto& u : users) {
        file << u.id << "|" << u.username << "|" << u.password_hash << "|"
             << (u.must_change_password ? "1" : "0") << "|"
             << (u.enabled ? "1" : "0") << "|" << u.role << "\n";
    }
}

std::vector<auth::AuthUser> Storage::load_auth_users() {
    std::vector<auth::AuthUser> users;
    std::ifstream file(auth_users_file());
    if (!file.is_open()) {
        return users;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        auth::AuthUser u;
        if (std::getline(ss, token, '|')) u.id = std::stoull(token);
        if (std::getline(ss, token, '|')) u.username = token;
        if (std::getline(ss, token, '|')) u.password_hash = token;
        if (std::getline(ss, token, '|')) u.must_change_password = (token == "1");
        if (std::getline(ss, token, '|')) u.enabled = (token == "1");
        if (std::getline(ss, token, '|')) u.role = token;
        u.name = u.username;
        users.push_back(std::move(u));
    }
    return users;
}

void Storage::save_access_grants(const std::vector<access::AccessGrant>& grants) {
    std::ofstream file(access_grants_file());
    for (const auto& g : grants) {
        file << g.id << "|" << g.access_user_id << "|" << g.site_id << "|"
             << access::permission_to_string(g.permission) << "\n";
    }
}

std::vector<access::AccessGrant> Storage::load_access_grants() {
    std::vector<access::AccessGrant> grants;
    std::ifstream file(access_grants_file());
    if (!file.is_open()) {
        return grants;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        access::AccessGrant g;
        if (std::getline(ss, token, '|')) g.id = std::stoull(token);
        if (std::getline(ss, token, '|')) g.access_user_id = std::stoull(token);
        if (std::getline(ss, token, '|')) g.site_id = std::stoull(token);
        if (std::getline(ss, token, '|')) g.permission = access::permission_from_string(token);
        g.name = std::to_string(g.access_user_id) + "-" + std::to_string(g.site_id);
        grants.push_back(std::move(g));
    }
    return grants;
}

void Storage::save_reverse_proxies(const std::vector<proxy::ReverseProxy>& proxies) {
    std::ofstream file(reverse_proxies_file());
    for (const auto& p : proxies) {
        file << p.id << "|" << p.domain << "|" << p.site_id << "|"
             << p.provider << "|" << p.config_path << "|" << p.upstream << "|"
             << (p.enabled ? "1" : "0") << "|" << p.status << "\n";
    }
}

std::vector<proxy::ReverseProxy> Storage::load_reverse_proxies() {
    std::vector<proxy::ReverseProxy> proxies;
    std::ifstream file(reverse_proxies_file());
    if (!file.is_open()) {
        return proxies;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        proxy::ReverseProxy p;
        if (std::getline(ss, token, '|')) p.id = std::stoull(token);
        if (std::getline(ss, token, '|')) p.domain = token;
        if (std::getline(ss, token, '|')) p.site_id = std::stoull(token);
        if (std::getline(ss, token, '|')) p.provider = token;
        if (std::getline(ss, token, '|')) p.config_path = token;
        if (std::getline(ss, token, '|')) p.upstream = token;
        if (std::getline(ss, token, '|')) p.enabled = (token == "1");
        if (std::getline(ss, token, '|')) p.status = token;
        p.name = p.domain;
        proxies.push_back(std::move(p));
    }
    return proxies;
}

std::string Storage::profiles_file() const {
    return db_path_ + "profiles.db";
}

std::string Storage::template_profiles_file() const {
    return db_path_ + "template_profiles.db";
}

void Storage::save_profiles(const std::vector<profile::Profile>& profiles) {
    std::ofstream file(profiles_file());
    for (const auto& p : profiles) {
        file << p.id << "|" << p.profile_name << "|"
             << profile::profile_type_to_string(p.type) << "|"
             << p.web_server << "|" << p.runtime << "|"
             << p.template_path << "|" << p.description << "|"
             << (p.enabled ? "1" : "0") << "|" << (p.default_profile ? "1" : "0") << "\n";
    }
}

std::vector<profile::Profile> Storage::load_profiles() {
    std::vector<profile::Profile> profiles;
    std::ifstream file(profiles_file());
    if (!file.is_open()) {
        return profiles;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        profile::Profile p;
        if (std::getline(ss, token, '|')) p.id = std::stoull(token);
        if (std::getline(ss, token, '|')) p.profile_name = token;
        if (std::getline(ss, token, '|')) p.type = profile::profile_type_from_string(token);
        if (std::getline(ss, token, '|')) p.web_server = token;
        if (std::getline(ss, token, '|')) p.runtime = token;
        if (std::getline(ss, token, '|')) p.template_path = token;
        if (std::getline(ss, token, '|')) p.description = token;
        if (std::getline(ss, token, '|')) p.enabled = (token == "1");
        if (std::getline(ss, token, '|')) p.default_profile = (token == "1");
        p.name = p.profile_name;
        profiles.push_back(std::move(p));
    }
    return profiles;
}

std::vector<profile::Profile> Storage::migrate_template_profiles() {
    std::vector<profile::Profile> profiles;
    std::ifstream file(template_profiles_file());
    if (!file.is_open()) return profiles;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string token;
        profile::Profile p;
        p.type = profile::ProfileType::WEB_SERVER;
        if (std::getline(ss, token, '|')) p.id = std::stoull(token);
        if (std::getline(ss, token, '|')) p.profile_name = token;
        if (std::getline(ss, token, '|')) p.web_server = token;
        if (std::getline(ss, token, '|')) p.runtime = token;
        if (std::getline(ss, token, '|')) p.template_path = token;
        if (std::getline(ss, token, '|')) p.description = token;
        if (std::getline(ss, token, '|')) p.enabled = (token == "1");
        if (std::getline(ss, token, '|')) p.default_profile = (token == "1");
        p.name = p.profile_name;
        profiles.push_back(std::move(p));
    }
    return profiles;
}

} // namespace containercp::storage
