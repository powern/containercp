#include "Storage.h"

#include <fstream>
#include <sstream>
#include <string>

namespace containercp::storage {

Storage::Storage(const std::string& db_path)
    : db_path_(db_path)
{
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
        file << s.id << "|" << s.domain << "|" << s.owner << "|" << s.node_id << "\n";
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
             << b.created_at << "|" << b.status << "\n";
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
        b.name = b.filename;
        backups.push_back(std::move(b));
    }
    return backups;
}

} // namespace containercp::storage
